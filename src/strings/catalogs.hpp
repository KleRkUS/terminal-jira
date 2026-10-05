// Copyright (C) 2026 the terminal-jira authors
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

// The compiled-in catalogs. Each is the whole string table for one language as
// JSON (with // comments allowed), defined in its own file.
//
// To add a language: copy en.cpp, translate the values, declare it here, and
// list it in kBuiltins in translations.cpp.
namespace translations {

extern const char kStringsEn[];

}  // namespace translations
