Drop Pleco .txt exports in this folder, then open the app and choose

    menu -> import Pleco lists

Format is Pleco's own tab-separated export, the same one the build pipeline
reads:

    //Book/Chapter
    headword<TAB>pinyin<TAB>definition

The //Book/Chapter line names the list. A file with no // line is imported as
one list named after the file. Re-importing is safe: entries are unique per
list, so nothing duplicates.

Exports written by "my lists -> export all" land in ../export/ in this same
format, ready to import back into Pleco.
