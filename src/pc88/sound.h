// ---------------------------------------------------------------------------
//	M88 - PC-8801 Emulator
//	Copyright (C) cisc 1997, 2001.
// ---------------------------------------------------------------------------
//	$Id: sound.h,v 1.28 2003/05/12 22:26:35 cisc Exp $

#pragma once

#include "device.h"
#include "file.h"
#include "sndbuf2.h"
#include "srcbuf.h"
#include "lpf.h"

// ---------------------------------------------------------------------------

class PC88;
class Scheduler;

namespace PC8801
{
class Sound;
class Config;

class Sound 
: public Device, public ISoundControl, protected SoundSourceL
{
public:
	Sound();
	~Sound();
	
	bool Init(PC88* pc, uint rate, int bufsize);
	void Cleanup();	
	
	void ApplyConfig(const Config* config);
	bool SetRate(uint rate, int bufsize);

	void IOCALL UpdateCounter(uint);
	
	bool IFCALL Connect(ISoundSource* src);
	bool IFCALL Disconnect(ISoundSource* src);
	bool IFCALL Update(ISoundSource* src=0);
	int  IFCALL GetSubsampleTime(ISoundSource* src);

	void FillWhenEmpty(bool f) { soundbuf.FillWhenEmpty(f); }
	void PrimeBuffer(int samples);

	SoundSource* GetSoundSource() { return &soundbuf; }
	int GetOutput(Sample* dest, int nsamples);

	int		Get(Sample* dest, int size);
	int		Get(SampleL* dest, int size);
	ulong	GetRate() { return mixrate; }
	int		GetChannels() { return 2; }
	
	int		GetAvail() { return INT_MAX; }
	int		GetRingAvail();
	int		GetRingSize() const { return buffersize; }
	uint	GetOutputSampleRate() const { return samplingrate; }

	bool	IsDumping() const { return dump_active_; }
	bool	DumpBegin(const char* path);
	void	DumpEnd();

protected:
	uint mixrate;
	uint samplingrate;		// サンプリングレート
	uint rate50;			// samplingrate / 50

private:
	struct SSNode
	{
		ISoundSource* ss;
		SSNode* next;
	};

//	SoundBuffer2 soundbuf;
	SamplingRateConverter soundbuf;

	PC88* pc;
	
	int32* mixingbuf;
	int buffersize;
	
	uint32 prevtime;
	uint32 cfgflg;
	int tdiff;
	uint mixthreshold;

	bool enabled;

	void RebuildLpf();

	IIR_LPF lpf_;
	bool lpf_enabled_;
	uint lpf_fc_;
	uint lpf_order_;
	
	SSNode* sslist;
	CriticalSection cs_ss;

	void RecordDumpSamples(const Sample* data, int frames);

	bool dump_active_;
	uint32 dump_bytes_;
	FileIO dump_file_;
	CriticalSection cs_dump_;
};

}

