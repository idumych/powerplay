/*
 * PowerPlay — the Library window.
 *
 * Copyright 2026 the PowerPlay authors. MIT licence.
 */
#include "LibraryWindow.h"

#include <Application.h>
#include <Bitmap.h>
#include <Button.h>
#if __has_include(<ColumnListView.h>)
#	include <ColumnListView.h>
#	include <ColumnTypes.h>
#else
	// Haiku has BColumnListView in libbe with private headers.
#	include <private/interface/ColumnListView.h>
#	include <private/interface/ColumnTypes.h>
#endif
#include <Deskbar.h>
#include <Directory.h>
#include <Entry.h>
#include <File.h>
#include <FindDirectory.h>
#include <LayoutBuilder.h>
#include <MediaFile.h>
#include <MediaTrack.h>
#include <MenuBar.h>
#include <MenuItem.h>
#include <Path.h>
#include <Slider.h>
#include <SplitView.h>
#include <StringView.h>
#include <TextControl.h>

#include <condition_variable>
#include <ctype.h>
#include <deque>
#include <math.h>
#include <mutex>
#include <set>
#include <stdio.h>
#include <string.h>
#include <strings.h>
#include <thread>

#include "AudioTags.h"
#include "Constants.h"
#include "PlayHead.h"
#include "Skins.h"

static const char* kCacheName = "PowerPlay_library_cache";

static const uint32 kMsgSourceChanged = 'LwSc';
static const uint32 kMsgSongInvoked = 'LwIv';
static const uint32 kMsgSearchChanged = 'LwSe';
static const uint32 kMsgVolumeChanged = 'LwVo';
static const uint32 kMsgSongInfo = 'LwIn';
static const uint32 kMsgFind = 'LwFd';
static const uint32 kMsgSelectAll = 'LwSA';
static const uint32 kMsgRemoveSelected = 'LwRm';

// The song list's columns
enum {
	kPlayingColumn = 0,
	kSongColumn,
	kTimeColumn,
	kArtistColumn,
	kAlbumColumn
};


//	#pragma mark - Icons


enum IconShape {
	kPlayIcon, kPauseIcon, kPreviousIcon, kNextIcon, kAddIcon,
	kShuffleIcon, kRepeatIcon, kEqualizerIcon
};


// A button's symbol, drawn at the size of the font in the control text
// colour.
static BBitmap*
MakeIcon(IconShape shape, float size)
{
	size = roundf(size);
	BBitmap* bitmap = new BBitmap(BRect(0, 0, size - 1, size - 1),
		B_BITMAP_ACCEPTS_VIEWS, B_RGBA32);
	if (bitmap->InitCheck() != B_OK) {
		delete bitmap;
		return NULL;
	}
	memset(bitmap->Bits(), 0, bitmap->BitsLength());
	BView* view = new BView(bitmap->Bounds(), "icon", B_FOLLOW_NONE,
		B_WILL_DRAW);
	bitmap->Lock();
	bitmap->AddChild(view);
	view->SetDrawingMode(B_OP_ALPHA);
	view->SetBlendingMode(B_PIXEL_ALPHA, B_ALPHA_COMPOSITE);
	view->SetHighColor(ui_color(B_CONTROL_TEXT_COLOR));
	view->SetLineMode(B_ROUND_CAP, B_ROUND_JOIN);

	float s = size;
	auto at = [s](float x, float y) { return BPoint(x * s, y * s); };
	auto box = [s](float l, float t, float r, float b) {
		return BRect(l * s, t * s, r * s, b * s);
	};
	switch (shape) {
		case kPlayIcon:
			view->FillTriangle(at(0.25, 0.12), at(0.25, 0.88),
				at(0.88, 0.5));
			break;
		case kPauseIcon:
			view->FillRect(box(0.2, 0.15, 0.42, 0.85));
			view->FillRect(box(0.58, 0.15, 0.8, 0.85));
			break;
		case kPreviousIcon:
			view->FillRect(box(0.08, 0.2, 0.18, 0.8));
			view->FillTriangle(at(0.55, 0.2), at(0.55, 0.8), at(0.18, 0.5));
			view->FillTriangle(at(0.92, 0.2), at(0.92, 0.8), at(0.55, 0.5));
			break;
		case kNextIcon:
			view->FillRect(box(0.82, 0.2, 0.92, 0.8));
			view->FillTriangle(at(0.45, 0.2), at(0.45, 0.8), at(0.82, 0.5));
			view->FillTriangle(at(0.08, 0.2), at(0.08, 0.8), at(0.45, 0.5));
			break;
		case kAddIcon:
			view->FillRect(box(0.42, 0.12, 0.58, 0.88));
			view->FillRect(box(0.12, 0.42, 0.88, 0.58));
			break;
		case kShuffleIcon:
			// Two paths crossing, each ending in an arrowhead
			view->SetPenSize(fmaxf(1, s * 0.1f));
			view->StrokeLine(at(0.08, 0.28), at(0.3, 0.28));
			view->StrokeLine(at(0.3, 0.28), at(0.6, 0.72));
			view->StrokeLine(at(0.6, 0.72), at(0.75, 0.72));
			view->StrokeLine(at(0.08, 0.72), at(0.3, 0.72));
			view->StrokeLine(at(0.3, 0.72), at(0.6, 0.28));
			view->StrokeLine(at(0.6, 0.28), at(0.75, 0.28));
			view->FillTriangle(at(0.7, 0.1), at(0.7, 0.46), at(0.96, 0.28));
			view->FillTriangle(at(0.7, 0.54), at(0.7, 0.9), at(0.96, 0.72));
			break;
		case kRepeatIcon:
			// A loop with an arrowhead on top
			view->SetPenSize(fmaxf(1, s * 0.1f));
			view->StrokeRoundRect(box(0.1, 0.28, 0.9, 0.78), s * 0.2f,
				s * 0.2f);
			view->FillTriangle(at(0.48, 0.1), at(0.48, 0.46), at(0.72, 0.28));
			break;
		case kEqualizerIcon:
		{
			// Three faders
			view->SetPenSize(fmaxf(1, s * 0.08f));
			const float x[] = {0.2f, 0.5f, 0.8f};
			const float knob[] = {0.62f, 0.3f, 0.5f};
			for (int i = 0; i < 3; i++) {
				view->StrokeLine(at(x[i], 0.1), at(x[i], 0.9));
				view->FillRect(box(x[i] - 0.12f, knob[i] - 0.08f,
					x[i] + 0.12f, knob[i] + 0.08f));
			}
			break;
		}
	}
	view->Sync();
	bitmap->RemoveChild(view);
	delete view;
	bitmap->Unlock();
	return bitmap;
}


static BButton*
IconButton(const char* name, IconShape shape, float size, BMessage* message)
{
	BButton* button = new BButton(name, NULL, message);
	BBitmap* icon = MakeIcon(shape, size);
	if (icon != NULL) {
		button->SetIcon(icon);
		delete icon;
	}
	return button;
}


static void
SetIcon(BButton* button, IconShape shape, float size)
{
	BBitmap* icon = MakeIcon(shape, size);
	if (icon != NULL) {
		button->SetIcon(icon);
		delete icon;
	}
}


//	#pragma mark - Columns


// The playing song's mark: a small triangle.
class PlayingField : public BField {
public:
	PlayingField(bool playing) : fPlaying(playing) {}
	bool Playing() const { return fPlaying; }
	void SetPlaying(bool playing) { fPlaying = playing; }
private:
	bool fPlaying;
};


class PlayingColumn : public BTitledColumn {
public:
	PlayingColumn(float width)
		:
		BTitledColumn("", width, width, width, B_ALIGN_CENTER)
	{
	}

	virtual void DrawField(BField* field, BRect rect, BView* parent)
	{
		if (!static_cast<PlayingField*>(field)->Playing())
			return;
		float size = floorf(fminf(rect.Width(), rect.Height()) * 0.5f);
		BPoint center((rect.left + rect.right) / 2,
			(rect.top + rect.bottom) / 2);
		parent->SetDrawingMode(B_OP_OVER);
		parent->FillTriangle(center + BPoint(-size / 2, -size / 2),
			center + BPoint(-size / 2, size / 2), center + BPoint(size / 2, 0));
	}

	virtual int CompareFields(BField* a, BField* b)
	{
		return int(static_cast<PlayingField*>(b)->Playing())
			- int(static_cast<PlayingField*>(a)->Playing());
	}

	virtual bool AcceptsField(const BField* field) const
	{
		return dynamic_cast<const PlayingField*>(field) != NULL;
	}
};


// "3:25", sorted by the seconds.
class TimeField : public BStringField {
public:
	TimeField(double seconds)
		:
		BStringField(_Format(seconds)),
		fSeconds(seconds)
	{
	}

	double Seconds() const { return fSeconds; }

private:
	static BString _Format(double seconds)
	{
		if (seconds <= 0)
			return "";
		char text[32];
		audio::FormatTime(seconds, text, sizeof(text));
		return text;
	}

	double fSeconds;
};


class TimeColumn : public BStringColumn {
public:
	TimeColumn(const char* title, float width)
		:
		BStringColumn(title, width, width / 2, width * 2, B_TRUNCATE_END,
			B_ALIGN_RIGHT)
	{
	}

	virtual int CompareFields(BField* a, BField* b)
	{
		double first = static_cast<TimeField*>(a)->Seconds();
		double second = static_cast<TimeField*>(b)->Seconds();
		return first < second ? -1 : first > second ? 1 : 0;
	}

	virtual bool AcceptsField(const BField* field) const
	{
		return dynamic_cast<const TimeField*>(field) != NULL;
	}
};


// Strings sorted as people expect: case aside, and "Track 2" before
// "Track 10".
class NameColumn : public BStringColumn {
public:
	NameColumn(const char* title, float width)
		:
		BStringColumn(title, width, width / 4, width * 4, B_TRUNCATE_END)
	{
	}

	virtual int CompareFields(BField* a, BField* b)
	{
		return Compare(static_cast<BStringField*>(a)->String(),
			static_cast<BStringField*>(b)->String());
	}

	static int Compare(const char* a, const char* b)
	{
		// Empty last
		if (a[0] == '\0' || b[0] == '\0')
			return (a[0] == '\0') - (b[0] == '\0');
		while (*a != '\0' && *b != '\0') {
			if (isdigit((unsigned char)*a) && isdigit((unsigned char)*b)) {
				char* endA;
				char* endB;
				unsigned long long x = strtoull(a, &endA, 10);
				unsigned long long y = strtoull(b, &endB, 10);
				if (x != y)
					return x < y ? -1 : 1;
				a = endA;
				b = endB;
				continue;
			}
			int x = tolower((unsigned char)*a);
			int y = tolower((unsigned char)*b);
			if (x != y)
				return x < y ? -1 : 1;
			a++;
			b++;
		}
		return (*a != '\0') - (*b != '\0');
	}
};


// A row of the song list: which entry of the library or playlist it
// shows.
class SongRow : public BRow {
public:
	SongRow(const std::string& path, int32 index, float height)
		:
		BRow(height),
		fPath(path),
		fIndex(index)
	{
	}

	const std::string& Path() const { return fPath; }
	int32 Index() const { return fIndex; }

private:
	std::string fPath;
	int32 fIndex;
};


//	#pragma mark - NowPlayingView


// The display at the top: what's playing, who by, and how far through,
// in a rounded panel. Click or drag along the bar to go elsewhere in the
// song.
class NowPlayingView : public BView {
public:
	NowPlayingView()
		:
		BView("now playing", B_WILL_DRAW | B_FULL_UPDATE_ON_RESIZE),
		fHasFile(false),
		fPosition(0),
		fDuration(0),
		fTracking(false)
	{
		SetViewColor(B_TRANSPARENT_COLOR);
	}

	void SetSong(const BString& title, const BString& detail, bool hasFile)
	{
		if (title == fTitle && detail == fDetail && hasFile == fHasFile)
			return;
		fTitle = title;
		fDetail = detail;
		fHasFile = hasFile;
		Invalidate();
	}

	void SetPosition(double position, double duration)
	{
		if (fTracking)
			return;
		// Redraw when the seconds or the bar change
		BRect bar = _Bar();
		bool changed = (int)position != (int)fPosition
			|| duration != fDuration
			|| (duration > 0 && (int)(position / duration * bar.Width())
				!= (int)(fPosition / duration * bar.Width()));
		fPosition = position;
		fDuration = duration;
		if (changed)
			Invalidate();
	}

	virtual void Draw(BRect updateRect)
	{
		BRect bounds = Bounds();
		rgb_color panel = Parent() != NULL ? Parent()->ViewColor()
			: ui_color(B_PANEL_BACKGROUND_COLOR);
		SetHighColor(panel);
		FillRect(updateRect);

		SetDrawingMode(B_OP_ALPHA);
		float radius = roundf(bounds.Height() / 4);
		BRect lcd = bounds.InsetByCopy(0.5f, 0.5f);
		SetHighColor(_LcdColor());
		FillRoundRect(lcd, radius, radius);
		SetHighColor(tint_color(panel, B_DARKEN_3_TINT));
		StrokeRoundRect(lcd, radius, radius);

		rgb_color text = ui_color(B_DOCUMENT_TEXT_COLOR);
		SetHighColor(text);
		BFont bold(be_bold_font);
		BFont plain(be_plain_font);
		font_height boldHeight, plainHeight;
		bold.GetHeight(&boldHeight);
		plain.GetHeight(&plainHeight);
		float width = bounds.Width() - 2 * radius;

		if (!fHasFile) {
			SetFont(&bold);
			float y = roundf((bounds.top + bounds.bottom
				+ boldHeight.ascent - boldHeight.descent) / 2);
			_DrawCentered(kAppName, y, width);
			return;
		}

		float line = ceilf(plainHeight.ascent + plainHeight.descent
			+ plainHeight.leading);
		float y = bounds.top + _Inset() + ceilf(boldHeight.ascent);
		SetFont(&bold);
		_DrawCentered(fTitle, y, width);
		SetFont(&plain);
		y += line;
		_DrawCentered(fDetail, y, width);

		// Elapsed, the bar, remaining
		char elapsed[32], remaining[32];
		audio::FormatTime(fPosition, elapsed, sizeof(elapsed));
		if (fDuration > 0) {
			remaining[0] = '-';
			audio::FormatTime(fmax(0, fDuration - fPosition), remaining + 1,
				sizeof(remaining) - 1);
		} else
			remaining[0] = '\0';
		BRect bar = _Bar();
		float baseline = roundf((bar.top + bar.bottom + plainHeight.ascent
			- plainHeight.descent) / 2);
		DrawString(elapsed, BPoint(bar.left - _TimeWidth()
			+ (_TimeWidth() - plain.StringWidth(elapsed)) / 2 - 4, baseline));
		DrawString(remaining, BPoint(bar.right + 4
			+ (_TimeWidth() - plain.StringWidth(remaining)) / 2, baseline));

		SetHighColor(tint_color(_LcdColor(), B_DARKEN_2_TINT));
		FillRect(bar);
		SetHighColor(text);
		StrokeRect(bar);
		if (fDuration > 0) {
			float x = bar.left + roundf(fmin(1, fPosition / fDuration)
				* bar.Width());
			FillRect(BRect(bar.left, bar.top, x, bar.bottom));
			// The thumb, a diamond
			float half = roundf(bar.Height() / 2) + 2;
			float middle = (bar.top + bar.bottom) / 2;
			FillTriangle(BPoint(x - half, middle), BPoint(x, middle - half),
				BPoint(x + half, middle));
			FillTriangle(BPoint(x - half, middle), BPoint(x, middle + half),
				BPoint(x + half, middle));
		}
	}

	virtual void MouseDown(BPoint where)
	{
		BRect bar = _Bar().InsetByCopy(-4, -6);
		if (!fHasFile || fDuration <= 0 || !bar.Contains(where))
			return;
		fTracking = true;
		SetMouseEventMask(B_POINTER_EVENTS, B_LOCK_WINDOW_FOCUS);
		_SeekTo(where.x);
	}

	virtual void MouseMoved(BPoint where, uint32 transit,
		const BMessage* dragMessage)
	{
		if (fTracking)
			_SeekTo(where.x);
	}

	virtual void MouseUp(BPoint where)
	{
		fTracking = false;
	}

	virtual BSize MinSize()
	{
		return BSize(be_plain_font->StringWidth("M") * 22, _Height());
	}

	virtual BSize MaxSize()
	{
		return BSize(be_plain_font->StringWidth("M") * 40, _Height());
	}

	virtual BSize PreferredSize()
	{
		return BSize(be_plain_font->StringWidth("M") * 30, _Height());
	}

private:
	float _Inset() const
	{
		return roundf(be_plain_font->Size() / 3);
	}

	float _Height() const
	{
		font_height plain, bold;
		be_plain_font->GetHeight(&plain);
		be_bold_font->GetHeight(&bold);
		float line = ceilf(plain.ascent + plain.descent + plain.leading);
		return ceilf(bold.ascent + bold.descent) + 2 * line + 2 * _Inset();
	}

	float _TimeWidth() const
	{
		return be_plain_font->StringWidth("-00:00:00");
	}

	BRect _Bar() const
	{
		BRect bounds = Bounds();
		font_height plain;
		be_plain_font->GetHeight(&plain);
		float line = ceilf(plain.ascent + plain.descent + plain.leading);
		float middle = bounds.bottom - _Inset() - roundf(line / 2);
		float half = fmaxf(2, roundf(be_plain_font->Size() / 5));
		float side = _TimeWidth() + roundf(bounds.Height() / 4) + 4;
		return BRect(bounds.left + side, middle - half, bounds.right - side,
			middle + half);
	}

	void _DrawCentered(const char* text, float y, float width)
	{
		BFont font;
		GetFont(&font);
		BString line(text);
		font.TruncateString(&line, B_TRUNCATE_END, width);
		float x = roundf((Bounds().Width() - font.StringWidth(line)) / 2);
		DrawString(line, BPoint(x, y));
	}

	rgb_color _LcdColor() const
	{
		// A pale olive, the colour of a calculator's display; darker on a
		// dark desktop.
		rgb_color base = ui_color(B_DOCUMENT_BACKGROUND_COLOR);
		bool dark = base.Brightness() < 128;
		rgb_color tint = dark ? make_color(80, 92, 60)
			: make_color(206, 216, 170);
		return mix_color(base, tint, dark ? 80 : 150);
	}

	void _SeekTo(float x)
	{
		BRect bar = _Bar();
		float fraction = (x - bar.left) / fmaxf(1, bar.Width());
		fraction = fminf(1, fmaxf(0, fraction));
		fPosition = fraction * fDuration;
		Invalidate();
		BMessage seek(kMsgSeek);
		seek.AddFloat("fraction", fraction);
		be_app->PostMessage(&seek);
	}

	BString fTitle;
	BString fDetail;
	bool fHasFile;
	double fPosition;
	double fDuration;
	bool fTracking;
};


//	#pragma mark - InfoLoader


// Reads songs' tags and lengths in a thread of its own, and sends the
// window what it found (kMsgSongInfo), several songs at a time.
class InfoLoader {
public:
	InfoLoader(BMessenger target)
		:
		fTarget(target),
		fQuit(false),
		fThread(&InfoLoader::_Run, this)
	{
	}

	~InfoLoader()
	{
		{
			std::lock_guard<std::mutex> lock(fLock);
			fQuit = true;
		}
		fWake.notify_one();
		fThread.join();
	}

	// Reads the file unless it hasn't changed since modified.
	void Add(const std::string& path, int64 modified)
	{
		{
			std::lock_guard<std::mutex> lock(fLock);
			fQueue.emplace_back(path, modified);
		}
		fWake.notify_one();
	}

private:
	void _Run()
	{
		BMessage info(kMsgSongInfo);
		int32 count = 0;
		while (true) {
			std::pair<std::string, int64> job;
			{
				std::unique_lock<std::mutex> lock(fLock);
				// Send what's found when the queue runs dry, or every 64
				if (count > 0 && (fQueue.empty() || count >= 64)) {
					lock.unlock();
					if (!_Send(info))
						return;
					info.MakeEmpty();
					count = 0;
					lock.lock();
				}
				fWake.wait(lock, [this] {
					return fQuit || !fQueue.empty();
				});
				if (fQuit)
					return;
				job = fQueue.front();
				fQueue.pop_front();
			}
			if (_Load(job.first, job.second, info))
				count++;
		}
	}

	// Never waits on a window that has stopped reading: it may be waiting
	// for this thread to end.
	bool _Send(BMessage& info)
	{
		while (true) {
			status_t status = fTarget.SendMessage(&info, (BHandler*)NULL,
				100000);
			if (status != B_TIMED_OUT && status != B_WOULD_BLOCK)
				return status == B_OK;
			std::lock_guard<std::mutex> lock(fLock);
			if (fQuit)
				return false;
		}
	}

	static bool _Load(const std::string& path, int64 cachedModified,
		BMessage& info)
	{
		BEntry entry(path.c_str(), true);
		struct stat st;
		if (entry.GetStat(&st) != B_OK)
			return false;
		if ((int64)st.st_mtime == cachedModified)
			return false;

		BFile file(&entry, B_READ_ONLY);
		off_t size = 0;
		if (file.InitCheck() != B_OK || file.GetSize(&size) != B_OK)
			return false;
		audio::Tags tags;
		audio::ReadTags([&file](uint64_t offset, void* buffer, size_t bytes)
				-> size_t {
				ssize_t read = file.ReadAt(offset, buffer, bytes);
				return read < 0 ? 0 : (size_t)read;
			}, size, tags);

		// Haiku keeps songs' details in attributes too
		BString value;
		if (tags.title.empty()
			&& file.ReadAttrString("Audio:Title", &value) == B_OK)
			tags.title = value.String();
		if (tags.artist.empty()
			&& file.ReadAttrString("Audio:Artist", &value) == B_OK)
			tags.artist = value.String();
		if (tags.album.empty()
			&& file.ReadAttrString("Audio:Album", &value) == B_OK)
			tags.album = value.String();

		if (tags.duration <= 0) {
			entry_ref ref;
			entry.GetRef(&ref);
			BMediaFile media(&ref);
			for (int32 i = 0; media.InitCheck() == B_OK
					&& i < media.CountTracks(); i++) {
				BMediaTrack* track = media.TrackAt(i);
				if (track == NULL)
					continue;
				media_format format;
				if (track->EncodedFormat(&format) == B_OK
					&& format.IsAudio()) {
					tags.duration = track->Duration() / 1000000.0;
					media.ReleaseTrack(track);
					break;
				}
				media.ReleaseTrack(track);
			}
		}

		// One of each per song
		info.AddString("path", path.c_str());
		info.AddString("title", tags.title.c_str());
		info.AddString("artist", tags.artist.c_str());
		info.AddString("album", tags.album.c_str());
		info.AddInt32("track", tags.track);
		info.AddDouble("duration", tags.duration);
		info.AddInt64("modified", (int64)st.st_mtime);
		return true;
	}

	BMessenger fTarget;
	std::mutex fLock;
	std::condition_variable fWake;
	std::deque<std::pair<std::string, int64>> fQueue;
	bool fQuit;
	std::thread fThread;
};


//	#pragma mark - LibraryWindow


static BMessage*
Command(uint32 what)
{
	return new BMessage(what);
}


static float
RowHeight()
{
	font_height height;
	be_plain_font->GetHeight(&height);
	return ceilf(height.ascent + height.descent + height.leading) + 4;
}


LibraryWindow::LibraryWindow(BRect frame, const BMessage& state)
	:
	BWindow(frame, kAppName, B_TITLED_WINDOW,
		B_AUTO_UPDATE_SIZE_LIMITS | B_ASYNCHRONOUS_CONTROLS),
	fSource(kLibrarySource),
	fCurrent(-1),
	fPlaying(false),
	fLoader(NULL),
	fCacheChanged(false)
{
	BMenuBar* menuBar = new BMenuBar("menu bar");

	BMenu* menu = new BMenu("File");
	menu->AddItem(new BMenuItem("Add to library" B_UTF8_ELLIPSIS,
		Command(kMsgAddToLibrary), 'O'));
	menu->AddItem(new BMenuItem("Add to playlist" B_UTF8_ELLIPSIS,
		Command(kMsgAddFiles), 'O', B_SHIFT_KEY));
	menu->AddItem(new BMenuItem("Equalizer", Command(kMsgShowEqualizer), 'E'));
	menu->AddSeparatorItem();
	menu->AddItem(new BMenuItem("About PowerPlay" B_UTF8_ELLIPSIS,
		Command(kMsgAbout)));
	menu->AddSeparatorItem();
	menu->AddItem(new BMenuItem("Quit", Command(B_QUIT_REQUESTED), 'Q'));
	menu->SetTargetForItems(be_app);
	menuBar->AddItem(menu);

	menu = new BMenu("Edit");
	menu->AddItem(new BMenuItem("Select all", Command(kMsgSelectAll), 'A'));
	menu->AddItem(new BMenuItem("Remove", Command(kMsgRemoveSelected)));
	menu->AddSeparatorItem();
	menu->AddItem(new BMenuItem("Find", Command(kMsgFind), 'F'));
	menu->SetTargetForItems(this);
	menuBar->AddItem(menu);

	menu = new BMenu("Playback");
	fPlayItem = new BMenuItem("Play", Command(kMsgPlayPause));
	menu->AddItem(fPlayItem);
	menu->AddItem(new BMenuItem("Stop", Command(kMsgStop)));
	menu->AddSeparatorItem();
	menu->AddItem(new BMenuItem("Previous", Command(kMsgPrevious),
		B_LEFT_ARROW));
	menu->AddItem(new BMenuItem("Next", Command(kMsgNext), B_RIGHT_ARROW));
	menu->AddSeparatorItem();
	fShuffleItem = new BMenuItem("Shuffle", Command(kMsgToggleShuffle));
	menu->AddItem(fShuffleItem);
	fRepeatItem = new BMenuItem("Repeat playlist", Command(kMsgToggleRepeat));
	menu->AddItem(fRepeatItem);
	menu->SetTargetForItems(be_app);
	menuBar->AddItem(menu);

	menu = new BMenu("Settings");
	fDeskbarItem = new BMenuItem("Show meter in Deskbar",
		Command(kMsgToggleDeskbar));
	menu->AddItem(fDeskbarItem);
	fLivelyItem = new BMenuItem("Lively needles", Command(kMsgToggleLively));
	menu->AddItem(fLivelyItem);
	menu->SetTargetForItems(be_app);
	menuBar->AddItem(menu);

	fSkinsMenu = new_skins_menu(kLibraryLook);
	menuBar->AddItem(fSkinsMenu);

	// The transport and volume, top left
	float big = roundf(be_plain_font->Size() * 1.5f);
	float small = roundf(be_plain_font->Size() * 1.1f);
	fPreviousButton = IconButton("previous", kPreviousIcon, big,
		Command(kMsgPrevious));
	fPlayButton = IconButton("play", kPlayIcon, big, Command(kMsgPlayPause));
	fNextButton = IconButton("next", kNextIcon, big, Command(kMsgNext));
	BButton* transport[] = {fPreviousButton, fPlayButton, fNextButton};
	for (BButton* button : transport)
		button->SetTarget(be_app);

	fVolume = new BSlider("volume", NULL, Command(kMsgVolumeChanged), 0, 100,
		B_HORIZONTAL);
	fVolume->SetModificationMessage(Command(kMsgVolumeChanged));
	fVolume->SetExplicitMaxSize(BSize(be_plain_font->StringWidth("M") * 9,
		B_SIZE_UNSET));

	fDisplay = new NowPlayingView;

	fSearch = new BTextControl("search", NULL, "", NULL);
	fSearch->SetModificationMessage(Command(kMsgSearchChanged));
	fSearch->SetExplicitMinSize(BSize(be_plain_font->StringWidth("M") * 10,
		B_SIZE_UNSET));
	fSearch->SetExplicitMaxSize(BSize(be_plain_font->StringWidth("M") * 14,
		B_SIZE_UNSET));
	BStringView* searchLabel = new BStringView("search label", "Search");
	searchLabel->SetAlignment(B_ALIGN_CENTER);
	searchLabel->SetExplicitMaxSize(BSize(B_SIZE_UNLIMITED, B_SIZE_UNSET));

	// The sources, left
	fSources = new BColumnListView("sources", 0, B_FANCY_BORDER, false);
	fSources->SetSortingEnabled(false);
	// Flat lists: no room for outline latches
	fSources->SetLatchWidth(0);
	fSources->SetSelectionMode(B_SINGLE_SELECTION_LIST);
	float em = be_plain_font->StringWidth("M");
	fSources->AddColumn(new BStringColumn("Source", em * 10, em * 4, em * 40,
		B_TRUNCATE_END), 0);
	const char* sourceNames[] = {"Library", "Playlist"};
	for (const char* name : sourceNames) {
		BRow* row = new BRow(RowHeight());
		row->SetField(new BStringField(name), 0);
		fSources->AddRow(row);
	}
	fSources->SetSelectionMessage(Command(kMsgSourceChanged));
	fSources->SetExplicitMinSize(BSize(em * 8, B_SIZE_UNSET));

	// The songs
	fSongs = new BColumnListView("songs", 0, B_FANCY_BORDER, true);
	fSongs->SetSelectionMode(B_MULTIPLE_SELECTION_LIST);
	fSongs->SetSortingEnabled(true);
	fSongs->SetLatchWidth(0);
	fSongs->AddColumn(new PlayingColumn(roundf(em * 1.6f)), kPlayingColumn);
	fSongs->AddColumn(new NameColumn("Song", em * 18), kSongColumn);
	fSongs->AddColumn(new TimeColumn("Time",
		roundf(be_plain_font->StringWidth("00:00:00") + em * 1.5f)),
		kTimeColumn);
	fSongs->AddColumn(new NameColumn("Artist", em * 12), kArtistColumn);
	fSongs->AddColumn(new NameColumn("Album", em * 12), kAlbumColumn);
	fSongs->SetInvocationMessage(Command(kMsgSongInvoked));

	BSplitView* split = new BSplitView(B_HORIZONTAL, 0);
	BLayoutBuilder::Split<>(split)
		.Add(fSources, 1)
		.Add(fSongs, 4);
	split->SetCollapsible(false);

	// The bottom row
	BButton* add = IconButton("add", kAddIcon, small,
		Command(kMsgAddToLibrary));
	add->SetTarget(be_app);
	fShuffleButton = IconButton("shuffle", kShuffleIcon, small,
		Command(kMsgToggleShuffle));
	fShuffleButton->SetBehavior(BButton::B_TOGGLE_BEHAVIOR);
	fShuffleButton->SetTarget(be_app);
	fRepeatButton = IconButton("repeat", kRepeatIcon, small,
		Command(kMsgToggleRepeat));
	fRepeatButton->SetBehavior(BButton::B_TOGGLE_BEHAVIOR);
	fRepeatButton->SetTarget(be_app);
	BButton* equalizer = IconButton("equalizer", kEqualizerIcon, small,
		Command(kMsgShowEqualizer));
	equalizer->SetTarget(be_app);
	add->SetToolTip("Add to library");
	fShuffleButton->SetToolTip("Shuffle");
	fRepeatButton->SetToolTip("Repeat playlist");
	equalizer->SetToolTip("Equalizer");
	fSummary = new BStringView("summary", "");
	fSummary->SetAlignment(B_ALIGN_CENTER);
	fSummary->SetExplicitMaxSize(BSize(B_SIZE_UNLIMITED, B_SIZE_UNSET));

	BLayoutBuilder::Group<>(this, B_VERTICAL, 0)
		.Add(menuBar)
		.AddGroup(B_VERTICAL, B_USE_SMALL_SPACING)
			.SetInsets(B_USE_WINDOW_SPACING, B_USE_SMALL_SPACING,
				B_USE_WINDOW_SPACING, B_USE_WINDOW_SPACING)
			.AddGroup(B_HORIZONTAL)
				.AddGroup(B_VERTICAL, B_USE_SMALL_SPACING, 0)
					.AddGroup(B_HORIZONTAL, B_USE_SMALL_SPACING)
						.Add(fPreviousButton)
						.Add(fPlayButton)
						.Add(fNextButton)
					.End()
					.Add(fVolume)
				.End()
				.AddGlue(0.5f)
				.Add(fDisplay, 3)
				.AddGlue(0.5f)
				.AddGroup(B_VERTICAL, 0, 0)
					.AddGlue()
					.Add(fSearch)
					.Add(searchLabel)
					.AddGlue()
				.End()
			.End()
			.Add(split)
			.AddGroup(B_HORIZONTAL, B_USE_SMALL_SPACING)
				.Add(add)
				.Add(fShuffleButton)
				.Add(fRepeatButton)
				.Add(fSummary)
				.Add(equalizer)
			.End()
		.End();

	fSources->SetTarget(this);
	fSongs->SetTarget(this);

	BMessage columns;
	if (state.FindMessage("columns", &columns) == B_OK)
		fSongs->LoadState(&columns);

	_LoadCache();
	fLoader = new InfoLoader(BMessenger(this));
	ShowSource((Source)state.GetInt32("source", kLibrarySource));
	SetState(false, false);
	_UpdateDisplay();
}


LibraryWindow::~LibraryWindow()
{
	delete fLoader;
	_SaveCache();
}


bool
LibraryWindow::QuitRequested()
{
	be_app->PostMessage(B_QUIT_REQUESTED);
	return false;
}


void
LibraryWindow::MenusBeginning()
{
	fDeskbarItem->SetMarked(BDeskbar().HasItem(kDeskbarItemName));
	fill_skins_menu(fSkinsMenu, kLibraryLook);
}


void
LibraryWindow::DispatchMessage(BMessage* message, BHandler* handler)
{
	if (message->what == B_KEY_DOWN) {
		const char* bytes;
		int32 modifiers = message->GetInt32("modifiers", 0);
		if (message->FindString("bytes", &bytes) == B_OK
			&& (modifiers & (B_COMMAND_KEY | B_CONTROL_KEY
				| B_OPTION_KEY)) == 0) {
			bool typing = dynamic_cast<BTextView*>(CurrentFocus()) != NULL;
			// Space plays or pauses, unless it's typed or presses a button
			if (bytes[0] == ' ' && !typing
				&& dynamic_cast<BControl*>(CurrentFocus()) == NULL) {
				be_app->PostMessage(kMsgPlayPause);
				return;
			}
			if ((bytes[0] == B_DELETE || bytes[0] == B_BACKSPACE)
				&& _SongsFocused()) {
				_RemoveSelected();
				return;
			}
		}
	}
	BWindow::DispatchMessage(message, handler);
}


void
LibraryWindow::MessageReceived(BMessage* message)
{
	switch (message->what) {
		case kMsgLibraryUpdate:
			_UpdateLibrary(message);
			break;
		case kMsgPlaylistUpdate:
			_UpdatePlaylist(message);
			break;
		case kMsgSongInfo:
			_SongInfoArrived(message);
			break;

		case kMsgSourceChanged:
		{
			BRow* row = fSources->CurrentSelection();
			if (row == NULL) {
				// Always one source showing
				fSources->AddToSelection(fSources->RowAt(fSource));
				break;
			}
			Source source = (Source)fSources->IndexOf(row);
			if (source != fSource) {
				fSource = source;
				_Rebuild();
			}
			break;
		}

		case kMsgSongInvoked:
			_PlaySelected();
			break;

		case kMsgSearchChanged:
			_Rebuild();
			break;

		case kMsgFind:
			fSearch->MakeFocus(true);
			fSearch->TextView()->SelectAll();
			break;

		case kMsgSelectAll:
			_SelectAll();
			break;

		case kMsgRemoveSelected:
			_RemoveSelected();
			break;

		case kMsgVolumeChanged:
		{
			BMessage volume(kMsgSetVolume);
			volume.AddFloat("volume", fVolume->Value() / 100.0f);
			be_app->PostMessage(&volume);
			break;
		}

		case B_SIMPLE_DATA:
			// Dropped files go into the list showing
			if (message->HasRef("refs")) {
				BMessage add(*message);
				add.what = fSource == kLibrarySource
					? (uint32)kMsgLibraryAdd : (uint32)kMsgPlaylistAdd;
				be_app->PostMessage(&add);
			}
			break;

		default:
			BWindow::MessageReceived(message);
	}
}


void
LibraryWindow::SetState(bool playing, bool hasFile)
{
	if (playing != fPlaying) {
		SetIcon(fPlayButton, playing ? kPauseIcon : kPlayIcon,
			roundf(be_plain_font->Size() * 1.5f));
	}
	fPlaying = playing;
	fPlayItem->SetLabel(playing ? "Pause" : "Play");
	_UpdateDisplay();
}


void
LibraryWindow::SetPosition(double position, double duration)
{
	fDisplay->SetPosition(position, duration);
}


void
LibraryWindow::SetVolume(float volume)
{
	int32 value = (int32)lroundf(volume * 100);
	if (fVolume->Value() != value)
		fVolume->SetValue(value);
}


void
LibraryWindow::SetRepeat(bool repeat)
{
	fRepeatItem->SetMarked(repeat);
	fRepeatButton->SetValue(repeat ? B_CONTROL_ON : B_CONTROL_OFF);
}


void
LibraryWindow::SetShuffle(bool shuffle)
{
	fShuffleItem->SetMarked(shuffle);
	fShuffleButton->SetValue(shuffle ? B_CONTROL_ON : B_CONTROL_OFF);
}


void
LibraryWindow::SetLively(bool lively)
{
	fLivelyItem->SetMarked(lively);
}


void
LibraryWindow::ShowSource(Source source)
{
	if (source != kLibrarySource && source != kPlaylistSource)
		source = kLibrarySource;
	fSource = source;
	fSources->DeselectAll();
	fSources->AddToSelection(fSources->RowAt(source));
	_Rebuild();
}


void
LibraryWindow::SaveState(BMessage& state)
{
	BMessage columns;
	fSongs->SaveState(&columns);
	state.AddMessage("columns", &columns);
	state.AddInt32("source", fSource);
}


//	#pragma mark - private


void
LibraryWindow::_UpdateLibrary(const BMessage* message)
{
	std::vector<std::string> paths;
	const char* path;
	for (int32 i = 0; message->FindString("path", i, &path) == B_OK; i++)
		paths.push_back(path);
	if (paths == fLibrary)
		return;
	fLibrary.swap(paths);
	if (fSource == kLibrarySource)
		_Rebuild();
}


void
LibraryWindow::_UpdatePlaylist(const BMessage* message)
{
	std::vector<std::string> paths;
	const char* path;
	for (int32 i = 0; message->FindString("path", i, &path) == B_OK; i++)
		paths.push_back(path);
	int32 current = message->GetInt32("current", -1);
	bool changed = paths != fPlaylist;
	fPlaylist.swap(paths);
	fCurrent = current;
	if (changed && fSource == kPlaylistSource)
		_Rebuild();
	else
		_MarkPlaying();
	_UpdateDisplay();
}


// Fills the song list from the library or playlist, as far as the search
// lets through, keeping the selection.
void
LibraryWindow::_Rebuild()
{
	std::set<std::string> selected;
	for (BRow* row = fSongs->CurrentSelection(); row != NULL;
			row = fSongs->CurrentSelection(row))
		selected.insert(static_cast<SongRow*>(row)->Path());

	fSongs->Clear();
	const std::vector<std::string>& paths = fSource == kLibrarySource
		? fLibrary : fPlaylist;
	const std::string& playing = _PlayingPath();
	float height = RowHeight();
	BRow* firstSelected = NULL;
	for (size_t i = 0; i < paths.size(); i++) {
		if (!_Matches(paths[i]))
			continue;
		const SongInfo& info = _Info(paths[i]);
		SongRow* row = new SongRow(paths[i], (int32)i, height);
		bool isPlaying = fSource == kPlaylistSource
			? (int32)i == fCurrent : paths[i] == playing;
		row->SetField(new PlayingField(isPlaying), kPlayingColumn);
		row->SetField(new BStringField(info.title.c_str()), kSongColumn);
		row->SetField(new TimeField(info.duration), kTimeColumn);
		row->SetField(new BStringField(info.artist.c_str()), kArtistColumn);
		row->SetField(new BStringField(info.album.c_str()), kAlbumColumn);
		fSongs->AddRow(row);
		if (selected.count(paths[i]) != 0) {
			fSongs->AddToSelection(row);
			if (firstSelected == NULL)
				firstSelected = row;
		}
	}
	if (firstSelected != NULL)
		fSongs->ScrollTo(firstSelected);
	_UpdateSummary();
}


bool
LibraryWindow::_Matches(const std::string& path) const
{
	const char* search = fSearch->Text();
	if (search == NULL || search[0] == '\0')
		return true;
	auto found = fInfo.find(path);
	std::string title = found != fInfo.end() && !found->second.title.empty()
		? found->second.title : audio::TitleFromFileName(path);
	if (strcasestr(title.c_str(), search) != NULL)
		return true;
	if (found == fInfo.end())
		return false;
	return strcasestr(found->second.artist.c_str(), search) != NULL
		|| strcasestr(found->second.album.c_str(), search) != NULL;
}


void
LibraryWindow::_MarkPlaying()
{
	const std::string& playing = _PlayingPath();
	for (int32 i = 0; i < fSongs->CountRows(); i++) {
		SongRow* row = static_cast<SongRow*>(fSongs->RowAt(i));
		bool isPlaying = fSource == kPlaylistSource
			? row->Index() == fCurrent : row->Path() == playing;
		PlayingField* field
			= static_cast<PlayingField*>(row->GetField(kPlayingColumn));
		if (field->Playing() != isPlaying) {
			field->SetPlaying(isPlaying);
			fSongs->UpdateRow(row);
		}
	}
}


// "12 songs, 47.3 minutes", for what the list shows.
void
LibraryWindow::_UpdateSummary()
{
	int32 count = fSongs->CountRows();
	double seconds = 0;
	for (int32 i = 0; i < count; i++) {
		SongRow* row = static_cast<SongRow*>(fSongs->RowAt(i));
		auto found = fInfo.find(row->Path());
		if (found != fInfo.end())
			seconds += found->second.duration;
	}
	BString text;
	if (count == 0) {
		text = fSource == kLibrarySource && fLibrary.empty()
			? "Drop music here to add it to the library" : "No songs";
	} else {
		text.SetToFormat(count == 1 ? "%" B_PRId32 " song"
			: "%" B_PRId32 " songs", count);
		if (seconds >= 3600)
			text << BString().SetToFormat(", %.1f hours", seconds / 3600);
		else if (seconds > 0)
			text << BString().SetToFormat(", %.1f minutes", seconds / 60);
	}
	fSummary->SetText(text);
}


void
LibraryWindow::_UpdateDisplay()
{
	const std::string& path = _PlayingPath();
	if (path.empty()) {
		fDisplay->SetSong("", "", false);
		return;
	}
	const SongInfo& info = _Info(path);
	BString detail = info.artist.c_str();
	if (!info.album.empty()) {
		if (detail.Length() > 0)
			detail << " \xe2\x80\x94 ";
		detail << info.album.c_str();
	}
	fDisplay->SetSong(info.title.c_str(), detail, true);
}


void
LibraryWindow::_SongInfoArrived(const BMessage* message)
{
	std::set<std::string> changed;
	const char* path;
	for (int32 i = 0; message->FindString("path", i, &path) == B_OK; i++) {
		SongInfo& info = fInfo[path];
		info.title = message->GetString("title", i, "");
		if (info.title.empty())
			info.title = audio::TitleFromFileName(path);
		info.artist = message->GetString("artist", i, "");
		info.album = message->GetString("album", i, "");
		int32 track = 0;
		message->FindInt32("track", i, &track);
		info.track = track;
		double duration = 0;
		message->FindDouble("duration", i, &duration);
		info.duration = duration;
		int64 modified = 0;
		message->FindInt64("modified", i, &modified);
		info.modified = modified;
		info.loaded = true;
		changed.insert(path);
	}
	if (changed.empty())
		return;
	fCacheChanged = true;

	for (int32 i = 0; i < fSongs->CountRows(); i++) {
		SongRow* row = static_cast<SongRow*>(fSongs->RowAt(i));
		if (changed.count(row->Path()) == 0)
			continue;
		const SongInfo& info = fInfo[row->Path()];
		row->SetField(new BStringField(info.title.c_str()), kSongColumn);
		row->SetField(new TimeField(info.duration), kTimeColumn);
		row->SetField(new BStringField(info.artist.c_str()), kArtistColumn);
		row->SetField(new BStringField(info.album.c_str()), kAlbumColumn);
		fSongs->UpdateRow(row);
	}
	_UpdateSummary();
	if (changed.count(_PlayingPath()) != 0)
		_UpdateDisplay();
}


// Plays the song chosen: in the playlist, from where it is; in the
// library, the songs as listed become the playlist.
void
LibraryWindow::_PlaySelected()
{
	SongRow* chosen = static_cast<SongRow*>(fSongs->FocusRow());
	if (chosen == NULL || !chosen->IsSelected())
		chosen = static_cast<SongRow*>(fSongs->CurrentSelection());
	if (chosen == NULL)
		return;
	if (fSource == kPlaylistSource) {
		BMessage play(kMsgPlaylistPlay);
		play.AddInt32("index", chosen->Index());
		be_app->PostMessage(&play);
		return;
	}
	BMessage play(kMsgLibraryPlay);
	for (int32 i = 0; i < fSongs->CountRows(); i++) {
		SongRow* row = static_cast<SongRow*>(fSongs->RowAt(i));
		play.AddString("path", row->Path().c_str());
		if (row == chosen)
			play.AddInt32("index", i);
	}
	be_app->PostMessage(&play);
}


void
LibraryWindow::_RemoveSelected()
{
	BMessage remove(fSource == kLibrarySource ? (uint32)kMsgLibraryRemove
		: (uint32)kMsgPlaylistRemove);
	for (BRow* row = fSongs->CurrentSelection(); row != NULL;
			row = fSongs->CurrentSelection(row)) {
		SongRow* song = static_cast<SongRow*>(row);
		if (fSource == kLibrarySource)
			remove.AddString("path", song->Path().c_str());
		else
			remove.AddInt32("index", song->Index());
	}
	if (!remove.IsEmpty())
		be_app->PostMessage(&remove);
}


void
LibraryWindow::_SelectAll()
{
	if (dynamic_cast<BTextView*>(CurrentFocus()) != NULL) {
		static_cast<BTextView*>(CurrentFocus())->SelectAll();
		return;
	}
	for (int32 i = 0; i < fSongs->CountRows(); i++)
		fSongs->AddToSelection(fSongs->RowAt(i));
}


// What's known about a file: its tags once they've been read, its name
// until then.
const SongInfo&
LibraryWindow::_Info(const std::string& path)
{
	auto found = fInfo.find(path);
	if (found != fInfo.end()) {
		if (!found->second.loaded) {
			// From the cache: checked again in case the file changed
			found->second.loaded = true;
			fLoader->Add(path, found->second.modified);
		}
		return found->second;
	}
	SongInfo& info = fInfo[path];
	info.title = audio::TitleFromFileName(path);
	info.loaded = true;
	fLoader->Add(path, 0);
	return info;
}


const std::string&
LibraryWindow::_PlayingPath() const
{
	static const std::string kNone;
	if (fCurrent < 0 || fCurrent >= (int32)fPlaylist.size())
		return kNone;
	return fPlaylist[fCurrent];
}


bool
LibraryWindow::_SongsFocused()
{
	for (BView* view = CurrentFocus(); view != NULL; view = view->Parent()) {
		if (view == fSongs)
			return true;
	}
	return false;
}


// What was read last time, so a big library shows at once.
void
LibraryWindow::_LoadCache()
{
	BPath path;
	if (find_directory(B_USER_SETTINGS_DIRECTORY, &path) != B_OK
		|| path.Append(kCacheName) != B_OK)
		return;
	BFile file(path.Path(), B_READ_ONLY);
	BMessage cache;
	if (file.InitCheck() != B_OK || cache.Unflatten(&file) != B_OK)
		return;
	const char* songPath;
	for (int32 i = 0; cache.FindString("path", i, &songPath) == B_OK; i++) {
		SongInfo& info = fInfo[songPath];
		info.title = cache.GetString("title", i, "");
		info.artist = cache.GetString("artist", i, "");
		info.album = cache.GetString("album", i, "");
		int32 track = 0;
		cache.FindInt32("track", i, &track);
		info.track = track;
		double duration = 0;
		cache.FindDouble("duration", i, &duration);
		info.duration = duration;
		int64 modified = 0;
		cache.FindInt64("modified", i, &modified);
		info.modified = modified;
		// Checked against the file when first shown
		info.loaded = false;
	}
}


void
LibraryWindow::_SaveCache()
{
	if (!fCacheChanged)
		return;
	BPath path;
	if (find_directory(B_USER_SETTINGS_DIRECTORY, &path, true) != B_OK
		|| path.Append(kCacheName) != B_OK)
		return;
	// Only what's still in the library or the playlist
	std::set<std::string> keep(fLibrary.begin(), fLibrary.end());
	keep.insert(fPlaylist.begin(), fPlaylist.end());
	BMessage cache;
	for (const auto& entry : fInfo) {
		if (entry.second.modified == 0 || keep.count(entry.first) == 0)
			continue;
		cache.AddString("path", entry.first.c_str());
		cache.AddString("title", entry.second.title.c_str());
		cache.AddString("artist", entry.second.artist.c_str());
		cache.AddString("album", entry.second.album.c_str());
		cache.AddInt32("track", entry.second.track);
		cache.AddDouble("duration", entry.second.duration);
		cache.AddInt64("modified", entry.second.modified);
	}
	BFile file(path.Path(), B_WRITE_ONLY | B_CREATE_FILE | B_ERASE_FILE);
	if (file.InitCheck() == B_OK)
		cache.Flatten(&file);
}
