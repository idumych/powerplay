/*
 * PowerPlay — the playlist: an ordered list of files and the one playing.
 *
 * Copyright 2026 the PowerPlay authors. MIT licence.
 */
#include "Playlist.h"

#include <algorithm>
#include <cctype>
#include <random>
#include <sstream>
#include <strings.h>

namespace audio {


Playlist::Playlist()
	:
	fCurrent(kNone)
{
}


std::string
Playlist::NameAt(int index) const
{
	const std::string& path = fPaths[index];
	size_t slash = path.find_last_of('/');
	return slash == std::string::npos ? path : path.substr(slash + 1);
}


void
Playlist::SetCurrent(int index)
{
	fCurrent = index >= 0 && index < CountItems() ? index : kNone;
}


int
Playlist::Add(const std::string& path, int index)
{
	if (index < 0 || index > CountItems())
		index = CountItems();
	fPaths.insert(fPaths.begin() + index, path);
	if (fCurrent >= index)
		fCurrent++;
	return index;
}


void
Playlist::Remove(int index)
{
	if (index < 0 || index >= CountItems())
		return;
	fPaths.erase(fPaths.begin() + index);
	if (fCurrent == index)
		fCurrent = index < CountItems() ? index : kNone;
	else if (fCurrent > index)
		fCurrent--;
}


void
Playlist::Move(int from, int to)
{
	if (from < 0 || from >= CountItems() || to < 0 || to >= CountItems()
		|| from == to)
		return;
	std::string path = fPaths[from];
	fPaths.erase(fPaths.begin() + from);
	fPaths.insert(fPaths.begin() + to, path);
	if (fCurrent == from)
		fCurrent = to;
	else if (from < fCurrent && to >= fCurrent)
		fCurrent--;
	else if (from > fCurrent && to <= fCurrent)
		fCurrent++;
}


void
Playlist::Clear()
{
	fPaths.clear();
	fCurrent = kNone;
}


int
Playlist::Next(bool repeat) const
{
	if (CountItems() == 0)
		return kNone;
	if (fCurrent == kNone)
		return 0;
	if (fCurrent + 1 < CountItems())
		return fCurrent + 1;
	return repeat ? 0 : kNone;
}


int
Playlist::Previous(bool repeat) const
{
	if (CountItems() == 0)
		return kNone;
	if (fCurrent == kNone)
		return CountItems() - 1;
	if (fCurrent > 0)
		return fCurrent - 1;
	return repeat ? CountItems() - 1 : kNone;
}


void
Playlist::SortByName()
{
	_Sort(true);
}


void
Playlist::SortByPath()
{
	_Sort(false);
}


void
Playlist::_Sort(bool byName)
{
	std::string current = fCurrent != kNone ? fPaths[fCurrent] : "";
	auto key = [byName](const std::string& path) {
		size_t slash = path.find_last_of('/');
		return byName && slash != std::string::npos
			? path.substr(slash + 1) : path;
	};
	std::stable_sort(fPaths.begin(), fPaths.end(),
		[&](const std::string& a, const std::string& b) {
			return strcasecmp(key(a).c_str(), key(b).c_str()) < 0;
		});
	if (fCurrent != kNone) {
		fCurrent = int(std::find(fPaths.begin(), fPaths.end(), current)
			- fPaths.begin());
	}
}


void
Playlist::Shuffle(uint32_t seed)
{
	std::string current = fCurrent != kNone ? fPaths[fCurrent] : "";
	// Our own Fisher-Yates over a fixed generator, so the order doesn't
	// depend on the C++ library.
	std::mt19937 random(seed);
	for (int i = CountItems() - 1; i > 0; i--) {
		int j = int(random() % uint32_t(i + 1));
		std::swap(fPaths[i], fPaths[j]);
	}
	if (fCurrent != kNone) {
		fCurrent = int(std::find(fPaths.begin(), fPaths.end(), current)
			- fPaths.begin());
	}
}


std::string
Playlist::Save() const
{
	std::string text;
	for (const std::string& path : fPaths)
		text += path + "\n";
	return text;
}


void
Playlist::Load(const std::string& text, const std::string& baseFolder)
{
	Clear();
	std::istringstream lines(text);
	std::string line;
	while (std::getline(lines, line)) {
		while (!line.empty() && isspace((unsigned char)line.back()))
			line.pop_back();
		size_t start = 0;
		while (start < line.size() && isspace((unsigned char)line[start]))
			start++;
		line = line.substr(start);
		if (line.empty() || line[0] == '#')
			continue;
		if (line[0] != '/' && !baseFolder.empty())
			line = baseFolder + (baseFolder.back() == '/' ? "" : "/") + line;
		fPaths.push_back(line);
	}
}

}	// namespace audio
