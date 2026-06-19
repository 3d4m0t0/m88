// ---------------------------------------------------------------------------
//	M88 - PC-8801 Emulator
//	Copyright (C) cisc 1997, 2001.
// ---------------------------------------------------------------------------
//	$Id: sound.cpp,v 1.32 2003/05/19 01:10:32 cisc Exp $

#include "headers.h"
#include "types.h"
#include "misc.h"
#include "pc88/sound.h"
#include "pc88/pc88.h"
#include "pc88/config.h"
#include "file.h"

//#define LOGNAME "sound"
#include "diag.h"

using namespace PC8801;

// ---------------------------------------------------------------------------
//	生成・破棄
//
Sound::Sound()
: Device(0), sslist(0), mixingbuf(0), enabled(false), cfgflg(0), lpf_enabled_(false),
  lpf_fc_(8000), lpf_order_(4), dump_active_(false), dump_bytes_(0)
{
}

Sound::~Sound()
{
	Cleanup();
}

// ---------------------------------------------------------------------------
//	初期化とか
//
bool Sound::Init(PC88* pc88, uint rate, int bufsize)
{
	pc = pc88;
	// Match OPNIF::TimeEvent (scheduler time), not cpu1.GetCount() alone.
	prevtime = static_cast<uint32>(pc->GetTime());
	enabled = false;
	mixthreshold = 16;
	
	if (!SetRate(rate, bufsize))
		return false;

	// Periodic Mix while a beep port is held; edge-only Update() is not enough alone.
	pc88->AddEvent(5000, this, STATIC_CAST(TimeFunc, &Sound::UpdateCounter), 0, true);
	return true;
}

// ---------------------------------------------------------------------------
//	レート設定
//	clock:		OPN に与えるクロック
//	bufsize:	バッファ長 (サンプル単位?)
//
bool Sound::SetRate(uint rate, int bufsize)
{
	mixrate = 55467;

	// 各音源のレート設定を変更
	for (SSNode* n = sslist; n; n = n->next)
		n->ss->SetRate(mixrate);
	
	enabled = false;
	
	// 古いバッファを削除
	soundbuf.Cleanup();
	delete[] mixingbuf;	mixingbuf = 0;

	// 新しいバッファを用意
	samplingrate = rate;
	buffersize = bufsize;
	if (bufsize > 0)
	{
//		if (!soundbuf.Init(this, bufsize))
//			return false;
		if (!soundbuf.Init(this, bufsize, rate))
			return false;

		mixingbuf = new int32[2 * bufsize];
		if (!mixingbuf)
			return false;

		rate50 = mixrate / 50;
		tdiff = 0;
		if (pc) {
			prevtime = static_cast<uint32>(pc->GetTime());
		}
		enabled = true;
	}
	RebuildLpf();
	return true;
}

void Sound::PrimeBuffer(int samples)
{
	if (!enabled || samples <= 0) {
		return;
	}
	samples = Min(samples, buffersize / 2);
	if (samples > 0) {
		soundbuf.Fill(samples);
	}
}

// ---------------------------------------------------------------------------
//	後片付け
//
void Sound::Cleanup()
{
	DumpEnd();

	// 各音源を切り離す。(音源自体の削除は行わない)
	for (SSNode* n = sslist; n; )
	{
		SSNode* next = n->next;
		delete[] n;
		n = next;
	}
	sslist = 0;

	// バッファを開放
	soundbuf.Cleanup();
	delete[] mixingbuf; mixingbuf = 0;
}

// ---------------------------------------------------------------------------
//	音合成
//
int Sound::Get(Sample* dest, int nsamples)
{
	int mixsamples = Min(nsamples, buffersize);
	if (mixsamples > 0)
	{
		// 合成
		{
			memset(mixingbuf, 0, mixsamples * 2 * sizeof(int32));
			CriticalSection::Lock lock(cs_ss);
			for (SSNode* s = sslist; s; s = s->next)
				s->ss->Mix(mixingbuf, mixsamples);
		}

		int32* src = mixingbuf;
		for (int n = mixsamples; n>0; n--)
		{
			*dest++ = Limit(*src++, 32767, -32768);
			*dest++ = Limit(*src++, 32767, -32768);
		}
	}
	return mixsamples;
}

// ---------------------------------------------------------------------------
//	音合成
//
int Sound::Get(SampleL* dest, int nsamples)
{
	memset(dest, 0, static_cast<size_t>(nsamples) * 2 * sizeof(int32));

	CriticalSection::Lock lock(cs_ss);
	for (SSNode* s = sslist; s; s = s->next) {
		s->ss->Mix(dest, nsamples);
	}
	return nsamples;
}


// ---------------------------------------------------------------------------
//	設定更新
//
void Sound::ApplyConfig(const Config* config)
{
	if (config->flags & Config::mixsoundalways) {
		mixthreshold = 0;
	} else {
		mixthreshold = (config->flags & Config::precisemixing) ? 100 : 2000;
	}
	lpf_enabled_ = (config->flag2 & Config::lpfenable) != 0;
	lpf_fc_ = config->lpffc ? config->lpffc : 8000;
	lpf_order_ = config->lpforder ? config->lpforder : 4;
	RebuildLpf();
}

void Sound::RebuildLpf()
{
	if (lpf_enabled_ && samplingrate > 0) {
		lpf_.MakeFilter(lpf_fc_, samplingrate, lpf_order_);
	}
}

int Sound::GetRingAvail()
{
	return enabled ? soundbuf.GetAvail() : 0;
}

uint32 Sound::GetEmuClockTicks() const
{
	return pc ? static_cast<uint32>(pc->GetTime()) : 0;
}

int Sound::GetOutput(Sample* dest, int nsamples)
{
	const int got = soundbuf.Get(dest, nsamples);
	if (got <= 0) {
		return got;
	}
	if (lpf_enabled_) {
		for (int i = 0; i < got; ++i) {
			Sample* frame = dest + i * 2;
			frame[0] = static_cast<Sample>(Limit(lpf_.Filter(0, frame[0]), 32767, -32768));
			frame[1] = static_cast<Sample>(Limit(lpf_.Filter(1, frame[1]), 32767, -32768));
		}
	}
	if (dump_active_.load(std::memory_order_acquire)) {
		RecordDumpSamples(dest, got);
	}
	return got;
}

namespace {

#pragma pack(push, 1)
struct WavPcmHeader {
	char riff_id[4];
	uint32 riff_size;
	char wave_id[4];
	char fmt_id[4];
	uint32 fmt_size;
	uint16_t format;
	uint16_t channels;
	uint32 sample_rate;
	uint32 byte_rate;
	uint16_t block_align;
	uint16_t bits_per_sample;
	char data_id[4];
	uint32 data_size;
};
#pragma pack(pop)

constexpr uint32 kWavHeaderSize = sizeof(WavPcmHeader);

WavPcmHeader MakeWavHeader(uint rate, uint32 data_bytes)
{
	WavPcmHeader hdr {};
	hdr.riff_id[0] = 'R'; hdr.riff_id[1] = 'I'; hdr.riff_id[2] = 'F'; hdr.riff_id[3] = 'F';
	hdr.wave_id[0] = 'W'; hdr.wave_id[1] = 'A'; hdr.wave_id[2] = 'V'; hdr.wave_id[3] = 'E';
	hdr.fmt_id[0] = 'f'; hdr.fmt_id[1] = 'm'; hdr.fmt_id[2] = 't'; hdr.fmt_id[3] = ' ';
	hdr.fmt_size = 16;
	hdr.format = 1;
	hdr.channels = 2;
	hdr.sample_rate = rate;
	hdr.bits_per_sample = 16;
	hdr.block_align = static_cast<uint16>(hdr.channels * (hdr.bits_per_sample / 8));
	hdr.byte_rate = rate * hdr.block_align;
	hdr.data_id[0] = 'd'; hdr.data_id[1] = 'a'; hdr.data_id[2] = 't'; hdr.data_id[3] = 'a';
	hdr.data_size = data_bytes;
	hdr.riff_size = 36 + data_bytes;
	return hdr;
}

void FinalizeWavDump(FileIO& file, uint32& bytes, uint rate)
{
	if (!file.IsOpen()) {
		bytes = 0;
		return;
	}
	const WavPcmHeader hdr = MakeWavHeader(rate, bytes);
	file.Seek(0, FileIO::begin);
	file.Write(&hdr, static_cast<int32>(kWavHeaderSize));
	file.Close();
	bytes = 0;
}

bool OpenWavDump(FileIO& file, const char* path, uint rate)
{
	if (!path || !*path || rate == 0) {
		return false;
	}
	if (!file.Open(path, FileIO::create)) {
		return false;
	}
	const WavPcmHeader hdr = MakeWavHeader(rate, 0);
	if (file.Write(&hdr, static_cast<int32>(kWavHeaderSize)) !=
	    static_cast<int32>(kWavHeaderSize)) {
		file.Close();
		return false;
	}
	return true;
}

}  // namespace

bool Sound::DumpBegin(const char* path)
{
	if (!path || !*path || samplingrate == 0) {
		return false;
	}

	CriticalSection::Lock lock(cs_dump_);
	if (dump_active_.load(std::memory_order_acquire)) {
		return false;
	}

	if (!OpenWavDump(dump_file_, path, samplingrate)) {
		return false;
	}

	dump_bytes_ = 0;
	dump_active_.store(true, std::memory_order_release);
	return true;
}

void Sound::DumpEnd()
{
	CriticalSection::Lock lock(cs_dump_);
	if (!dump_active_.load(std::memory_order_acquire)) {
		return;
	}

	dump_active_.store(false, std::memory_order_release);
	FinalizeWavDump(dump_file_, dump_bytes_, samplingrate);
}

void Sound::RecordDumpSamples(const Sample* data, int frames)
{
	if (!data || frames <= 0) {
		return;
	}

	CriticalSection::Lock lock(cs_dump_);
	if (!dump_active_.load(std::memory_order_acquire) || !dump_file_.IsOpen()) {
		return;
	}

	const int32 bytes = static_cast<int32>(frames) * 2 * static_cast<int32>(sizeof(Sample));
	if (dump_file_.Write(data, bytes) == bytes) {
		dump_bytes_ += static_cast<uint32>(bytes);
	}
}

// ---------------------------------------------------------------------------
//	音源を追加する
//	Sound が持つ音源リストに，ss で指定された音源を追加，
//	ss の SetRate を呼び出す．
//
//	arg:	ss		追加する音源 (ISoundSource)
//	ret:	S_OK, E_FAIL, E_OUTOFMEMORY
//
bool Sound::Connect(ISoundSource* ss)
{
	CriticalSection::Lock lock(cs_ss);

	// 音源は既に登録済みか？;
	SSNode** n;
	for (n = &sslist; *n; n=&((*n)->next))
	{
		if ((*n)->ss == ss)
			return false;
	}
	
	SSNode* nn = new SSNode;
	if (nn)
	{
		*n = nn;
		nn->next = 0;
		nn->ss = ss;
		ss->SetRate(mixrate);
		return true;
	}
	return false;
}

// ---------------------------------------------------------------------------
//	音源リストから指定された音源を削除する
//
//	arg:	ss		削除する音源
//	ret:	S_OK, E_HANDLE
//
bool Sound::Disconnect(ISoundSource* ss)
{
	CriticalSection::Lock lock(cs_ss);
	
	for (SSNode** r = &sslist; *r; r=&((*r)->next))
	{
		if ((*r)->ss == ss)
		{
			SSNode* d = *r;
			*r = d->next;
			delete d;
			return true;
		}
	}
	return false;
}

// ---------------------------------------------------------------------------
//	更新処理
//	(指定された)音源の Mix を呼び出し，現在の時間まで更新する	
//	音源の内部状態が変わり，音が変化する直前の段階で呼び出すと
//	精度の高い音再現が可能になる(かも)．
//
//	arg:	src		更新する音源を指定(今の実装では無視されます)
//
bool Sound::Update(ISoundSource* /*src*/)
{
	const uint32 currenttime = static_cast<uint32>(pc->GetTime());
	const uint32 time = currenttime - prevtime;
	if (enabled && time > mixthreshold)
	{
		// nsamples = 経過時間(s) * サンプリングレート
		// sample = ticks * rate / clock / 100000
		// sample = ticks * (rate/50) / clock / 2000
		int a = MulDiv(static_cast<int>(time), rate50, pc->GetEffectiveSpeed()) + tdiff;
		int samples = a / 2000;
		tdiff = a % 2000;

		// Avoid one huge Mix (ADPCM glitches); retry remainder on next Update.
		enum { kMaxMixSamples = 2048 };
		if (samples > kMaxMixSamples)
		{
			tdiff += MulDiv((samples - kMaxMixSamples) * 2000,
			                pc->GetEffectiveSpeed(), rate50);
			samples = kMaxMixSamples;
		}

		if (samples > 0)
		{
			Log("Store = %5d samples\n", samples);
			const int filled = soundbuf.Fill(samples);
			if (filled < samples)
			{
				tdiff += MulDiv((samples - filled) * 2000,
				                pc->GetEffectiveSpeed(), rate50);
			}
			prevtime = currenttime;
		}
	}
	return true;
}

// ---------------------------------------------------------------------------
//	今まで合成された時間の，1サンプル未満の端数(0-1999)を求める
//
int IFCALL Sound::GetSubsampleTime(ISoundSource* /*src*/)
{
	return tdiff;
}

// ---------------------------------------------------------------------------
//	定期的に内部カウンタを更新
//
void IOCALL Sound::UpdateCounter(uint)
{
	if (static_cast<uint32>(pc->GetTime()) - prevtime > 10000)
	{
		Log("Update Counter\n");
		Update(0);
	}
}
