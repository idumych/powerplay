/*
 * PowerPlay — the VU meter as a Deskbar replicant.
 *
 * It lives in the Deskbar's team. It subscribes to PowerPlay for levels
 * and asks again every second while none arrive, so it picks up a
 * PowerPlay started later. Clicking it opens a menu of play controls.
 *
 * Copyright 2026 the PowerPlay authors. MIT licence.
 */
#ifndef POWERPLAY_DESKBAR_VU_H
#define POWERPLAY_DESKBAR_VU_H

#include "VUView.h"

class BMessageRunner;


class _EXPORT DeskbarVU : public VUView {
public:
								DeskbarVU();
								DeskbarVU(BMessage* archive);
	virtual						~DeskbarVU();

	static	DeskbarVU*			Instantiate(BMessage* archive);
	virtual	status_t			Archive(BMessage* archive,
									bool deep = true) const;

	virtual	void				AttachedToWindow();
	virtual	void				DetachedFromWindow();
	virtual	void				MessageReceived(BMessage* message);
	virtual	void				MouseDown(BPoint where);

private:
			void				_Subscribe();
			void				_ShowMenu(BPoint where);

			BMessageRunner*		fRunner;
			bigtime_t			fLastLevels;
			bool				fPlaying;
};

#endif	// POWERPLAY_DESKBAR_VU_H
