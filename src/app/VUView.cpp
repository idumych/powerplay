/*
 * PowerPlay — the VU meter face: two needles in a 48 x 16 box.
 *
 * Copyright 2026 the PowerPlay authors. MIT licence.
 */
#include "VUView.h"

#include <Bitmap.h>

#include "VUMeter.h"


VUView::VUView(const char* name, int32 scale)
	:
	BView(BRect(0, 0, vu::DeskbarFace::kWidth * scale - 1,
		vu::DeskbarFace::kHeight * scale - 1), name, B_FOLLOW_NONE,
		B_WILL_DRAW),
	fBitmap(NULL),
	fScale(scale),
	fLeft(0),
	fRight(0)
{
	_Init();
}


VUView::VUView(BMessage* archive)
	:
	BView(archive),
	fBitmap(NULL),
	fScale(1),
	fLeft(0),
	fRight(0)
{
	archive->FindInt32("scale", &fScale);
	_Init();
}


VUView::~VUView()
{
	delete fBitmap;
}


void
VUView::_Init()
{
	if (fScale < 1)
		fScale = 1;
	fBitmap = new BBitmap(BRect(0, 0, vu::DeskbarFace::kWidth * fScale - 1,
		vu::DeskbarFace::kHeight * fScale - 1), B_RGB32);
	// Draw() paints every pixel, so there's nothing to erase first.
	SetViewColor(B_TRANSPARENT_COLOR);
}


status_t
VUView::Archive(BMessage* archive, bool deep) const
{
	status_t status = BView::Archive(archive, deep);
	if (status == B_OK)
		status = archive->AddInt32("scale", fScale);
	return status;
}


void
VUView::Draw(BRect updateRect)
{
	// The face is drawn pixel by pixel by the portable core, so it looks
	// the same whatever the system's antialiasing settings are.
	if (fBitmap->InitCheck() != B_OK)
		return;
	vu::DeskbarFace::Render((uint32_t*)fBitmap->Bits(),
		fBitmap->BytesPerRow(), fScale, fLeft, fRight);
	DrawBitmap(fBitmap, BPoint(0, 0));
}


BSize
VUView::MinSize()
{
	return BSize(vu::DeskbarFace::kWidth * fScale - 1,
		vu::DeskbarFace::kHeight * fScale - 1);
}


BSize
VUView::MaxSize()
{
	return MinSize();
}


BSize
VUView::PreferredSize()
{
	return MinSize();
}


void
VUView::SetLevels(float left, float right)
{
	if (left == fLeft && right == fRight)
		return;
	fLeft = left;
	fRight = right;
	Invalidate();
}
