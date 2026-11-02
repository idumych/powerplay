/*
 * PowerPlay — host tests for the chunk cache, the play head and the
 * playlist.
 *
 * Copyright 2026 the PowerPlay authors. MIT licence.
 */
#include "PlayHead.h"
#include "Playlist.h"

#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <thread>
#include <vector>

static int sFailures = 0;

#define CHECK(cond, ...) do { \
	if (!(cond)) { \
		std::printf("FAIL %s:%d: ", __FILE__, __LINE__); \
		std::printf(__VA_ARGS__); \
		std::printf("\n"); \
		sFailures++; \
	} \
} while (0)

using audio::ChunkCache;
using audio::PlayHead;
using audio::Playlist;

static const int64_t kChunk = 1024;


// The test source: left = frame / 1e6, right = -frame / 1e6, so every
// output sample says exactly where the play head read.
static float
Left(double frame)
{
	return float(frame / 1e6);
}


static void
Fill(ChunkCache& cache, int64_t chunk, int64_t length)
{
	float* data = cache.BeginFill(chunk, chunk * kChunk);
	if (data == NULL)
		return;
	int64_t frames = std::min(kChunk, length - chunk * kChunk);
	for (int64_t i = 0; i < frames; i++) {
		int64_t frame = chunk * kChunk + i;
		data[i * cache.Channels()] = Left(frame);
		if (cache.Channels() > 1)
			data[i * cache.Channels() + 1] = -Left(frame);
	}
	cache.EndFill(chunk, frames);
}


static void
FillAll(ChunkCache& cache, int64_t length)
{
	for (int64_t chunk = 0; chunk * kChunk < length; chunk++)
		Fill(cache, chunk, length);
}


// Normal speed reads every frame exactly; double speed every other one.
static void
TestSpeeds()
{
	const int64_t length = 10 * kChunk;
	ChunkCache cache(2, kChunk, 12);
	cache.SetLength(length);
	FillAll(cache, length);

	const double speeds[] = {1.0, 2.0, 0.5, 4.0, 1.37};
	for (double speed : speeds) {
		PlayHead head;
		head.SetSpeed(speed);
		head.Seek(100);
		std::vector<float> out(2 * 2000);
		PlayHead::Result result = head.Render(cache, out.data(), 2000);
		CHECK(result == PlayHead::kPlaying, "speed %g: result %d", speed,
			result);
		double worst = 0;
		for (int i = 0; i < 2000; i++) {
			// The source is a straight line, which the cubic reproduces.
			double expected = Left(100 + i * speed);
			worst = std::max(worst, std::fabs(out[i * 2] - expected));
			worst = std::max(worst, std::fabs(out[i * 2 + 1] + expected));
		}
		CHECK(worst < 1e-6, "speed %g: off by %g", speed, worst);
		CHECK(std::fabs(head.Position() - (100 + 2000 * speed)) < 1e-6,
			"speed %g: position %g", speed, head.Position());
	}
}


// Backwards from the end to the start, sample for sample.
static void
TestReverse()
{
	const int64_t length = 3 * kChunk + 100;
	ChunkCache cache(2, kChunk, 8);
	cache.SetLength(length);
	FillAll(cache, length);

	PlayHead head;
	head.SetSpeed(-1);
	head.Seek(length - 1);
	std::vector<float> out(2 * length * 2);
	PlayHead::Result result = head.Render(cache, out.data(), length * 2);
	CHECK(result == PlayHead::kEnded, "reverse result %d", result);
	bool exact = true;
	for (int64_t i = 0; i < length; i++)
		exact = exact && std::fabs(out[i * 2] - Left(length - 1 - i)) < 1e-7;
	CHECK(exact, "reverse isn't the forward frames backwards");
	CHECK(out[length * 2] == 0 && out[(length + 50) * 2] == 0,
		"silence after the start");
	CHECK(head.Position() == 0, "position %g after reverse", head.Position());

	// Forwards to the end
	head.SetSpeed(1);
	head.Seek(length - 10);
	result = head.Render(cache, out.data(), 100);
	CHECK(result == PlayHead::kEnded, "forward end result %d", result);
	CHECK(std::fabs(out[9 * 2] - Left(length - 1)) < 1e-7,
		"last frame played");

	// Backwards from past the end (an estimate the file fell short of):
	// it starts from the real end instead of waiting for frames that
	// don't exist.
	head.SetSpeed(-1);
	head.Seek(length + 5000);
	result = head.Render(cache, out.data(), 10);
	CHECK(result == PlayHead::kPlaying
		&& std::fabs(out[0] - Left(length - 1)) < 1e-7,
		"reverse from past the end: result %d, first %g", result,
		out[0] * 1e6);
}


// A missing chunk waits; it doesn't skip.
static void
TestStarve()
{
	const int64_t length = 6 * kChunk;
	ChunkCache cache(2, kChunk, 8);
	cache.SetLength(length);
	Fill(cache, 0, length);

	PlayHead head;
	std::vector<float> out(2 * 3000);
	PlayHead::Result result = head.Render(cache, out.data(), 3000);
	CHECK(result == PlayHead::kStarved, "result %d", result);
	CHECK(head.Position() == kChunk - 1, "waits at %g", head.Position());
	CHECK(out[2 * 2000] == 0, "silence while starved");

	Fill(cache, 1, length);
	Fill(cache, 2, length);
	result = head.Render(cache, out.data(), 1000);
	CHECK(result == PlayHead::kPlaying, "result %d after filling", result);
	CHECK(std::fabs(out[0] - Left(kChunk - 1)) < 1e-7,
		"carries on where it waited: %g", out[0] * 1e6);
}


static void
TestRatesMonoVolume()
{
	const int64_t length = 4 * kChunk;
	ChunkCache cache(1, kChunk, 8);
	cache.SetLength(length);
	FillAll(cache, length);

	PlayHead head;
	head.SetRates(48000, 44100);
	head.SetVolume(0.5f);
	std::vector<float> out(2 * 1000);
	head.Render(cache, out.data(), 1000);
	double expected = Left(999 * 48000.0 / 44100.0) * 0.5;
	CHECK(std::fabs(out[999 * 2] - expected) < 1e-6,
		"48 kHz into 44.1 kHz: %g, expected %g", out[999 * 2], expected);
	CHECK(out[999 * 2] == out[999 * 2 + 1], "mono feeds both channels");

	// Balance turns the other side down
	ChunkCache stereo(2, kChunk, 8);
	stereo.SetLength(length);
	FillAll(stereo, length);
	PlayHead balanced;
	balanced.SetBalance(0.5f);
	std::vector<float> two(2 * 10);
	balanced.Seek(1000);
	balanced.Render(stereo, two.data(), 10);
	CHECK(std::fabs(two[0] - 0.5f * Left(1000)) < 1e-7
		&& std::fabs(two[1] + Left(1000)) < 1e-7, "balance right: %g %g",
		two[0] * 1e6, two[1] * 1e6);
	balanced.SetBalance(-3);
	CHECK(balanced.Balance() == -1, "balance clamped");

	head.SetSpeed(9);
	CHECK(head.Speed() == PlayHead::kMaxSpeed, "speed clamped");
	head.SetSpeed(0);
	double before = head.Position();
	CHECK(head.Render(cache, out.data(), 100) == PlayHead::kPlaying
		&& head.Position() == before && out[50] == 0, "speed 0 is silence");
}


static void
TestCache()
{
	ChunkCache cache(2, kChunk, 4);
	cache.SetLength(20 * kChunk);

	// Forwards it wants what's ahead, backwards what's behind.
	CHECK(cache.Wanted(5 * kChunk + 3, 1, 2) == 5, "current chunk first");
	Fill(cache, 5, 20 * kChunk);
	CHECK(cache.Wanted(5 * kChunk + 3, 1, 2) == 6, "next ahead");
	CHECK(cache.Wanted(5 * kChunk + 3, -1, 2) == 4, "next behind");
	Fill(cache, 4, 20 * kChunk);
	Fill(cache, 3, 20 * kChunk);
	Fill(cache, 6, 20 * kChunk);
	CHECK(cache.Wanted(5 * kChunk, -1, 2) == ChunkCache::kNoChunk,
		"all wanted ready");

	// Full: a chunk farther than everything cached isn't worth evicting
	// for; a nearer one evicts the farthest.
	CHECK(cache.BeginFill(12, 5 * kChunk) == NULL, "no room for chunk 12");
	CHECK(cache.BeginFill(7, 5 * kChunk) == NULL,
		"chunk 7 is farther than any cached one from chunk 5");
	CHECK(cache.BeginFill(7, 6 * kChunk) != NULL,
		"from chunk 6, chunk 7 evicts chunk 3");
	cache.EndFill(7, kChunk);
	int64_t frames;
	{
		std::lock_guard<std::mutex> _(cache.Lock());
		CHECK(cache.Chunk(7, frames) != NULL, "7 is in");
		CHECK(cache.Chunk(3, frames) == NULL && cache.Chunk(4, frames) != NULL,
			"chunk 3 went, 4 stayed");
	}

	// A chunk being decoded when the file changes is thrown away.
	float* data = cache.BeginFill(8, 8 * kChunk);
	if (data == NULL) {
		// Make room the simple way.
		cache.Clear();
		data = cache.BeginFill(8, 8 * kChunk);
	}
	CHECK(data != NULL, "fill 8");
	cache.Clear();
	cache.EndFill(8, kChunk);
	{
		std::lock_guard<std::mutex> _(cache.Lock());
		CHECK(cache.Chunk(8, frames) == NULL, "stale chunk dropped");
	}

	// The end, once found, wins over an estimate.
	cache.SetLength(10 * kChunk);
	cache.SetEnd(9 * kChunk + 5);
	CHECK(cache.Length() == 9 * kChunk + 5, "end %lld",
		(long long)cache.Length());
	cache.SetEnd(10 * kChunk);
	CHECK(cache.Length() == 9 * kChunk + 5, "a later end doesn't extend");
}


// A decoder thread against the audio side, forwards then backwards at 4x:
// every sample played is the right one, and starving is rare.
static void
TestThreaded()
{
	const int64_t length = 400 * kChunk;
	ChunkCache cache(2, kChunk, 16);
	cache.SetLength(length);
	PlayHead head;
	std::atomic<double> position(0);
	std::atomic<double> speed(1);
	std::atomic<bool> quit(false);

	std::thread decoder([&]() {
		while (!quit) {
			int64_t chunk = cache.Wanted(position, speed, 6);
			if (chunk == ChunkCache::kNoChunk) {
				std::this_thread::sleep_for(std::chrono::microseconds(200));
				continue;
			}
			float* data = cache.BeginFill(chunk, position);
			if (data == NULL) {
				std::this_thread::sleep_for(std::chrono::microseconds(200));
				continue;
			}
			int64_t frames = std::min(kChunk, length - chunk * kChunk);
			for (int64_t i = 0; i < frames; i++) {
				data[i * 2] = Left(chunk * kChunk + i);
				data[i * 2 + 1] = -Left(chunk * kChunk + i);
			}
			cache.EndFill(chunk, frames);
		}
	});

	int starved = 0, wrong = 0, blocks = 0;
	std::vector<float> out(2 * 512);
	auto run = [&](double newSpeed, int count) {
		head.SetSpeed(newSpeed);
		speed = newSpeed;
		for (int b = 0; b < count; b++) {
			double start = head.Position();
			PlayHead::Result result = head.Render(cache, out.data(), 512);
			position = head.Position();
			blocks++;
			// A sound card asks every 11.6 ms; ask every 1 ms, ten
			// times faster than real time.
			std::this_thread::sleep_for(std::chrono::milliseconds(1));
			if (result == PlayHead::kStarved) {
				starved++;
				std::this_thread::sleep_for(std::chrono::microseconds(500));
			}
			int played = int(std::lround((head.Position() - start)
				/ newSpeed));
			for (int i = 0; i < played; i++) {
				if (std::fabs(out[i * 2] - Left(start + i * newSpeed)) > 1e-6)
					wrong++;
			}
			if (result == PlayHead::kEnded)
				break;
		}
	};
	run(1.0, 200);
	run(4.0, 100);
	run(-4.0, 150);
	run(-1.0, 100);
	quit = true;
	decoder.join();

	CHECK(wrong == 0, "%d wrong samples", wrong);
	CHECK(starved < blocks / 10, "starved in %d of %d blocks", starved,
		blocks);
}


static void
TestFormatTime()
{
	char buffer[32];
	audio::FormatTime(0, buffer, sizeof(buffer));
	CHECK(strcmp(buffer, "00:00") == 0, "%s", buffer);
	audio::FormatTime(75.9, buffer, sizeof(buffer));
	CHECK(strcmp(buffer, "01:15") == 0, "%s", buffer);
	audio::FormatTime(3723, buffer, sizeof(buffer));
	CHECK(strcmp(buffer, "1:02:03") == 0, "%s", buffer);
	audio::FormatTime(-5, buffer, sizeof(buffer));
	CHECK(strcmp(buffer, "00:00") == 0, "%s", buffer);
}


static void
TestPlaylist()
{
	Playlist list;
	CHECK(list.Next(false) == Playlist::kNone, "empty next");
	list.Add("/music/b.wav");
	list.Add("/music/a/C.wav");
	list.Add("/other/a.wav", 0);
	CHECK(list.CountItems() == 3 && list.PathAt(0) == "/other/a.wav",
		"add at index");
	CHECK(list.NameAt(1) == "b.wav", "name %s", list.NameAt(1).c_str());

	CHECK(list.Next(false) == 0, "next from nothing is the first");
	list.SetCurrent(1);
	CHECK(list.Next(false) == 2 && list.Previous(false) == 0, "neighbours");
	list.SetCurrent(2);
	CHECK(list.Next(false) == Playlist::kNone && list.Next(true) == 0,
		"end, and repeat");
	list.SetCurrent(0);
	CHECK(list.Previous(false) == Playlist::kNone
		&& list.Previous(true) == 2, "start, and repeat");

	// The current entry follows edits.
	list.SetCurrent(1);		// b.wav
	list.Add("/x.wav", 0);
	CHECK(list.Current() == 2, "insert before current");
	list.Remove(0);
	CHECK(list.Current() == 1, "remove before current");
	list.Move(1, 2);
	CHECK(list.Current() == 2 && list.PathAt(2) == "/music/b.wav", "move it");
	list.Move(0, 2);
	CHECK(list.Current() == 1, "move past it");
	list.Remove(1);
	CHECK(list.Current() == 1, "removing it selects the next");

	list.Clear();
	list.Add("/z/b.wav");
	list.Add("/a/c.wav");
	list.Add("/m/A.wav");
	list.SetCurrent(0);
	list.SortByName();
	CHECK(list.PathAt(0) == "/m/A.wav" && list.PathAt(2) == "/a/c.wav"
		&& list.Current() == 1, "sort by name, case-insensitive");
	list.SortByPath();
	CHECK(list.PathAt(0) == "/a/c.wav" && list.PathAt(2) == "/z/b.wav"
		&& list.Current() == 2, "sort by path");

	Playlist a, b;
	for (int i = 0; i < 20; i++) {
		char path[16];
		snprintf(path, sizeof(path), "/%02d.wav", i);
		a.Add(path);
		b.Add(path);
	}
	a.Shuffle(42);
	b.Shuffle(42);
	CHECK(a.Save() == b.Save(), "same seed, same order");
	b.Shuffle(43);
	CHECK(a.Save() != b.Save(), "another seed, another order");

	Playlist loaded;
	loaded.Load("# PowerPlay\n/abs/one.wav\n  two.wav  \n\nsub/three.wav\n",
		"/base");
	CHECK(loaded.CountItems() == 3 && loaded.PathAt(0) == "/abs/one.wav"
		&& loaded.PathAt(1) == "/base/two.wav"
		&& loaded.PathAt(2) == "/base/sub/three.wav", "load");
	Playlist again;
	again.Load(loaded.Save());
	CHECK(again.Save() == loaded.Save(), "save and load round trip");
}


int
main()
{
	TestSpeeds();
	TestReverse();
	TestStarve();
	TestRatesMonoVolume();
	TestCache();
	TestThreaded();
	TestFormatTime();
	TestPlaylist();
	if (sFailures == 0)
		std::printf("all audio tests passed\n");
	return sFailures == 0 ? 0 : 1;
}
