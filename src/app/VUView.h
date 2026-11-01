/*
 * PowerPlay — the VU meter face: two needles in a 48 x 16 box.
 *
 * Copyright 2026 the PowerPlay authors. MIT licence.
 */
#ifndef POWERPLAY_VU_VIEW_H
#define POWERPLAY_VU_VIEW_H

#include <View.h>

class BBitmap;


class VUView : public BView {
public:
								VUView(const char* name, int32 scale = 1);
								VUView(BMessage* archive);
	virtual						~VUView();

	virtual	status_t			Archive(BMessage* archive,
									bool deep = true) const;
	virtual	void				Draw(BRect updateRect);
	virtual	BSize				MinSize();
	virtual	BSize				MaxSize();
	virtual	BSize				PreferredSize();

			void				SetLevels(float left, float right);

private:
			void				_Init();

			BBitmap*			fBitmap;
			int32				fScale;
			float				fLeft;
			float				fRight;
};

#endif	// POWERPLAY_VU_VIEW_H
