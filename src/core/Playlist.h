/*
 * PowerPlay — the playlist: an ordered list of files and the one playing.
 *
 * Portable: no Haiku headers, so the host tests can use it.
 *
 * Copyright 2026 the PowerPlay authors. MIT licence.
 */
#ifndef POWERPLAY_PLAYLIST_H
#define POWERPLAY_PLAYLIST_H

#include <cstdint>
#include <string>
#include <vector>

namespace audio {

class Playlist {
public:
	static const int kNone = -1;

								Playlist();

			int					CountItems() const
									{ return int(fPaths.size()); }
			const std::string&	PathAt(int index) const
									{ return fPaths[index]; }
			// The file name without its folder.
			std::string			NameAt(int index) const;

			// The entry playing (or to play next), or kNone.
			int					Current() const { return fCurrent; }
			void				SetCurrent(int index);

			// Adds at index (or the end); returns where it went.
			int					Add(const std::string& path,
									int index = kNone);
			void				Remove(int index);
			void				Move(int from, int to);
			void				Clear();

			// The entry after / before the current one, or kNone at the
			// end / start unless repeat wraps around.
			int					Next(bool repeat) const;
			int					Previous(bool repeat) const;

			void				SortByName();
			void				SortByPath();
			// Same seed, same order.
			void				Shuffle(uint32_t seed);

			// One path per line (an .m3u without extras); # lines are
			// comments. Relative paths are taken from baseFolder.
			std::string			Save() const;
			void				Load(const std::string& text,
									const std::string& baseFolder = "");

private:
			void				_Sort(bool byName);

			std::vector<std::string> fPaths;
			int					fCurrent;
};

}	// namespace audio

#endif	// POWERPLAY_PLAYLIST_H
