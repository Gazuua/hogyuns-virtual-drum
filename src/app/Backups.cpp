#include "Backups.h"

#include <algorithm>
#include <set>

#include "Text.h"

namespace
{

constexpr int maxMenuEntries = 30;
constexpr int maxFilesPerFolder = 500;

/** "20261125-221530" 꼴의 이름을 시각으로 바꾼다. 꼴이 다르면 false. */
bool timeFromName (const juce::String& name, juce::Time& time)
{
    if (name.length() != 15 || name[8] != '-' || ! (name.substring (0, 8) + name.substring (9)).containsOnly ("0123456789"))
        return false;

    time = juce::Time (name.substring (0, 4).getIntValue(), name.substring (4, 6).getIntValue() - 1, name.substring (6, 8).getIntValue(),
                       name.substring (9, 11).getIntValue(), name.substring (11, 13).getIntValue(), name.substring (13, 15).getIntValue());
    return true;
}

juce::String nameFromTime (juce::Time time)
{
    return time.formatted ("%Y%m%d-%H%M%S");
}

/** "오늘 22:15 · 3분 전", "어제 21:02", "11월 23일 14:20" */
juce::String describeTime (juce::Time time, juce::Time now)
{
    const auto clock = time.formatted ("%H:%M");
    const auto sameDay = [] (juce::Time a, juce::Time b)
    {
        return a.getYear() == b.getYear() && a.getDayOfYear() == b.getDayOfYear();
    };

    if (sameDay (time, now))
    {
        const auto minutes = static_cast<int> ((now - time).inMinutes());
        const auto ago = minutes < 1 ? ko ("방금") : minutes < 60 ? juce::String (minutes) + ko ("분 전")
                                                                  : juce::String (minutes / 60) + ko ("시간 전");
        return ko ("오늘 ") + clock + "  ·  " + ago;
    }

    if (sameDay (time, now - juce::RelativeTime::days (1.0)))
        return ko ("어제 ") + clock;

    return juce::String (time.getMonth() + 1) + ko ("월 ") + juce::String (time.getDayOfMonth()) + ko ("일 ") + clock;
}

} // namespace

//==============================================================================
void FileBackups::setFolders (const juce::File& backupsRoot, const juce::File& dataRootFolder)
{
    root = backupsRoot;
    dataRoot = dataRootFolder;
}

juce::File FileBackups::getFolderFor (const juce::File& original) const
{
    // 데이터 폴더 안의 파일은 그 안에서의 경로로, 밖의 파일은 파일 이름만으로 폴더 이름을 짓는다.
    auto name = original.isAChildOf (dataRoot) ? original.getRelativePathFrom (dataRoot) : original.getFileName();
    name = name.replaceCharacter ('\\', '/').replace ("/", "__");
    return root.getChildFile (juce::File::createLegalFileName (name));
}

std::vector<FileBackups::Entry> FileBackups::list (const juce::File& original) const
{
    std::vector<Entry> entries;

    if (! isEnabled())
        return entries;

    for (const auto& found : juce::RangedDirectoryIterator (getFolderFor (original), false, "*", juce::File::findFiles))
    {
        Entry entry;
        entry.file = found.getFile();

        if (timeFromName (entry.file.getFileNameWithoutExtension(), entry.time))
            entries.push_back (entry);
    }

    std::sort (entries.begin(), entries.end(), [] (const Entry& a, const Entry& b) { return a.time > b.time; });
    return entries;
}

bool FileBackups::keep (const juce::File& original, const juce::String& text, bool evenIfRecent, juce::Time now)
{
    if (! isEnabled() || text.isEmpty())
        return false;

    const auto folder = getFolderFor (original);
    const auto entries = list (original);

    if (! entries.empty())
    {
        const auto& newest = entries.front();

        if (! evenIfRecent && (now - newest.time).inSeconds() < static_cast<double> (minSecondsBetween))
            return false;

        if (newest.file.loadFileAsString() == text)
            return false;
    }

    if (! folder.createDirectory())
        return false;

    auto target = folder.getChildFile (nameFromTime (now) + original.getFileExtension());

    // 같은 초에 이미 하나 남겼으면(꼭 남기라고 한 경우) 1초 뒤의 이름을 쓴다.
    for (int extra = 1; target.exists() && extra < 60; ++extra)
        target = folder.getChildFile (nameFromTime (now + juce::RelativeTime::seconds (extra)) + original.getFileExtension());

    // 글자를 바꾸지 않고(줄바꿈도 그대로) 적는다.
    const auto* utf8 = text.toRawUTF8();

    if (! target.replaceWithData (utf8, text.getNumBytesAsUTF8()))
        return false;

    prune (folder, now);
    return true;
}

void FileBackups::prune (const juce::File& folder, juce::Time now) const
{
    std::vector<Entry> entries;

    for (const auto& found : juce::RangedDirectoryIterator (folder, false, "*", juce::File::findFiles))
    {
        Entry entry;
        entry.file = found.getFile();

        if (timeFromName (entry.file.getFileNameWithoutExtension(), entry.time))
            entries.push_back (entry);
    }

    std::sort (entries.begin(), entries.end(), [] (const Entry& a, const Entry& b) { return a.time > b.time; });

    // 새것부터 보면서, 같은 시간대(한 시간 또는 하루)에서는 가장 새것 하나만 남긴다.
    std::set<juce::int64> hoursKept, daysKept;
    auto kept = 0;

    for (const auto& entry : entries)
    {
        const auto age = now - entry.time;
        auto keepIt = true;

        if (kept >= maxFilesPerFolder)
            keepIt = false;
        else if (age.inHours() >= 48.0)
            keepIt = daysKept.insert (entry.time.getYear() * 1000 + entry.time.getDayOfYear()).second;
        else if (age.inHours() >= 1.0)
            keepIt = hoursKept.insert (entry.time.toMilliseconds() / 3600000).second;

        if (keepIt)
            ++kept;
        else
            entry.file.deleteFile();
    }
}

//==============================================================================
void showBackupMenu (juce::Component& button, const FileBackups& backups, const juce::File& original,
                     std::function<void (juce::File)> onChoose)
{
    const auto entries = backups.list (original);
    const auto now = juce::Time::getCurrentTime();

    juce::PopupMenu menu;
    menu.addSectionHeader (ko ("이전 상태로 되돌리기"));

    if (entries.empty())
        menu.addItem (1, ko ("아직 남은 백업이 없습니다"), false);

    for (std::size_t i = 0; i < entries.size() && i < static_cast<std::size_t> (maxMenuEntries); ++i)
        menu.addItem (100 + static_cast<int> (i), describeTime (entries[i].time, now));

    menu.addSeparator();
    menu.addItem (2, ko ("백업 폴더 열기"), backups.isEnabled());

    menu.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (&button),
                        [entries, folder = backups.getFolderFor (original), root = backups.getRoot(), onChoose = std::move (onChoose)] (int result)
                        {
                            if (result == 2)
                            {
                                if (folder.isDirectory())
                                {
                                    folder.revealToUser();
                                }
                                else if (root.createDirectory().wasOk())
                                {
                                    root.revealToUser();
                                }
                            }
                            else if (result >= 100 && static_cast<std::size_t> (result - 100) < entries.size() && onChoose != nullptr)
                            {
                                onChoose (entries[static_cast<std::size_t> (result - 100)].file);
                            }
                        });
}
