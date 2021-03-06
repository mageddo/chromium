// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/history/shortcuts_database.h"

#include <map>
#include <string>
#include <vector>

#include "base/logging.h"
#include "base/stringprintf.h"
#include "base/time.h"
#include "chrome/common/guid.h"
#include "sql/statement.h"

namespace {

// Using define instead of const char, so I could use ## in the statements.
#define kShortcutsDBName "omni_box_shortcuts"

void BindShortcutToStatement(
    const history::ShortcutsBackend::Shortcut& shortcut,
    sql::Statement* s) {
  DCHECK(guid::IsValidGUID(shortcut.id));
  s->BindString(0, shortcut.id);
  s->BindString16(1, shortcut.text);
  s->BindString(2, shortcut.url.spec());
  s->BindString16(3, shortcut.contents);
  s->BindString(4,
      AutocompleteMatch::ClassificationsToString(shortcut.contents_class));
  s->BindString16(5, shortcut.description);
  s->BindString(6,
      AutocompleteMatch::ClassificationsToString(shortcut.description_class));
  s->BindInt64(7, shortcut.last_access_time.ToInternalValue());
  s->BindInt(8, shortcut.number_of_hits);
}

bool DeleteShortcut(const char* field_name,
                    const std::string& id,
                    sql::Connection& db) {
  sql::Statement s(db.GetUniqueStatement(
      base::StringPrintf("DELETE FROM %s WHERE %s = ?", kShortcutsDBName,
                         field_name).c_str()));
  s.BindString(0, id);

  return s.Run();
}

}  // namespace

namespace history {

const FilePath::CharType ShortcutsDatabase::kShortcutsDatabaseName[] =
    FILE_PATH_LITERAL("Shortcuts");

ShortcutsDatabase::ShortcutsDatabase(const FilePath& folder_path)
    : database_path_(folder_path.Append(FilePath(kShortcutsDatabaseName))) {
}

bool ShortcutsDatabase::Init() {
  // Set the database page size to something a little larger to give us
  // better performance (we're typically seek rather than bandwidth limited).
  // This only has an effect before any tables have been created, otherwise
  // this is a NOP. Must be a power of 2 and a max of 8192.
  db_.set_page_size(4096);

  // Run the database in exclusive mode. Nobody else should be accessing the
  // database while we're running, and this will give somewhat improved perf.
  db_.set_exclusive_locking();

  // Attach the database to our index file.
  if (!db_.Open(database_path_))
    return false;

  if (!EnsureTable())
    return false;
  return true;
}

bool ShortcutsDatabase::AddShortcut(
    const ShortcutsBackend::Shortcut& shortcut) {
  sql::Statement s(db_.GetCachedStatement(SQL_FROM_HERE,
      "INSERT INTO " kShortcutsDBName
      " (id, text, url, contents, contents_class, description,"
      " description_class, last_access_time, number_of_hits) "
      "VALUES (?,?,?,?,?,?,?,?,?)"));
  BindShortcutToStatement(shortcut, &s);

  return s.Run();
}

bool ShortcutsDatabase::UpdateShortcut(
    const ShortcutsBackend::Shortcut& shortcut) {
  sql::Statement s(db_.GetCachedStatement(SQL_FROM_HERE,
    "UPDATE " kShortcutsDBName " "
      "SET id=?, text=?, url=?, contents=?, contents_class=?,"
      "     description=?, description_class=?, last_access_time=?,"
      "     number_of_hits=? "
      "WHERE id=?"));
  BindShortcutToStatement(shortcut, &s);
  s.BindString(9, shortcut.id);

  bool result = s.Run();
  DCHECK_GT(db_.GetLastChangeCount(), 0);
  return result;
}

bool ShortcutsDatabase::DeleteShortcutsWithIds(
    const std::vector<std::string>& shortcut_ids) {
  bool success = true;
  db_.BeginTransaction();
  for (std::vector<std::string>::const_iterator it = shortcut_ids.begin();
       it != shortcut_ids.end(); ++it) {
    if (!DeleteShortcut("id", *it, db_))
      success = false;
  }
  db_.CommitTransaction();
  return success;
}

bool ShortcutsDatabase::DeleteShortcutsWithUrl(
    const std::string& shortcut_url_spec) {
  return DeleteShortcut("url", shortcut_url_spec, db_);
}

bool ShortcutsDatabase::DeleteAllShortcuts() {
  if (!db_.Execute("DELETE FROM " kShortcutsDBName))
    return false;

  ignore_result(db_.Execute("VACUUM"));
  return true;
}

// Loads all of the shortcuts.
bool ShortcutsDatabase::LoadShortcuts(GuidToShortcutMap* shortcuts) {
  DCHECK(shortcuts);
  sql::Statement s(db_.GetCachedStatement(SQL_FROM_HERE,
      "SELECT id, text, url, contents, contents_class, "
      "description, description_class, last_access_time, number_of_hits "
      "FROM " kShortcutsDBName));

  if (!s.is_valid())
    return false;

  shortcuts->clear();
  while (s.Step()) {
    shortcuts->insert(std::make_pair(s.ColumnString(0),
        ShortcutsBackend::Shortcut(s.ColumnString(0), s.ColumnString16(1),
            GURL(s.ColumnString(2)), s.ColumnString16(3),
            AutocompleteMatch::ClassificationsFromString(s.ColumnString(4)),
            s.ColumnString16(5),
            AutocompleteMatch::ClassificationsFromString(s.ColumnString(6)),
            base::Time::FromInternalValue(s.ColumnInt64(7)), s.ColumnInt(8))));
  }
  return true;
}

ShortcutsDatabase::~ShortcutsDatabase() {}

bool ShortcutsDatabase::EnsureTable() {
  if (!db_.DoesTableExist(kShortcutsDBName)) {
    if (!db_.Execute(base::StringPrintf(
                     "CREATE TABLE %s ( "
                     "id VARCHAR PRIMARY KEY, "
                     "text VARCHAR, "
                     "url VARCHAR, "
                     "contents VARCHAR, "
                     "contents_class VARCHAR, "
                     "description VARCHAR, "
                     "description_class VARCHAR, "
                     "last_access_time INTEGER, "
                     "number_of_hits INTEGER)", kShortcutsDBName).c_str())) {
      NOTREACHED();
      return false;
    }
  }
  return true;
}

}  // namespace history
