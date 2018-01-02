/*
This file is part of DSpellCheck Plug-in for Notepad++
Copyright (C)2013 Sergey Semushin <Predelnik@gmail.com>

This program is free software; you can redistribute it and/or
modify it under the terms of the GNU General Public License
as published by the Free Software Foundation; either version 2
of the License, or (at your option) any later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with this program; if not, write to the Free Software
Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301, USA.
*/

#include "AspellInterface.h"
#include "CommonFunctions.h"
#include "LanguageInfo.h"
#include "MainDef.h"
#include "Plugin.h"
#include "aspell.h"

AspellInterface::~AspellInterface() = default;

namespace {
auto wrap_speller(AspellSpeller *raw_ptr) {
  return std::unique_ptr<AspellSpeller, void (*)(AspellSpeller *)>{
      raw_ptr, [](auto ptr) { delete_aspell_speller(ptr); }};
}

auto wrap_config(AspellConfig *raw_ptr) {
  return std::unique_ptr<AspellConfig, void (*)(AspellConfig *)>{
      raw_ptr, [](auto ptr) { delete_aspell_config(ptr); }};
}
} // namespace

AspellInterface::AspellInterface(HWND npp_window_arg)
    : m_single_speller(wrap_speller(nullptr)) {
  m_npp_window = npp_window_arg;
  m_last_selected_speller = nullptr;
  m_aspell_loaded = false;
}

std::vector<LanguageInfo> AspellInterface::get_language_list() const {
  if (!m_aspell_loaded) {
    return {};
  }

  auto asp_cfg = wrap_config(new_aspell_config());
  /* the returned pointer should _not_ need to be deleted */
  auto dlist = get_aspell_dict_info_list(asp_cfg.get());

  auto dels = aspell_dict_info_list_elements(dlist);

  if (aspell_dict_info_enumeration_at_end(dels) != 0) {
    delete_aspell_dict_info_enumeration(dels);
    return {};
  }

  const AspellDictInfo *entry;
  std::vector<LanguageInfo> names;
  while ((entry = aspell_dict_info_enumeration_next(dels)) != nullptr) {
    names.emplace_back(to_wstring(entry->name), false);
  }
  std::sort(names.begin(), names.end(), less_original);
  return names;
}

bool AspellInterface::is_working() const { return m_aspell_loaded; }

void AspellInterface::send_aspell_error(AspellCanHaveError *error) {
  MessageBox(m_npp_window, to_wstring(aspell_error_message(error)).c_str(),
             L"Aspell Error", MB_OK | MB_ICONEXCLAMATION);
}

void AspellInterface::setup_aspell_config(AspellConfig *spell_config) {
  aspell_config_replace(spell_config, "encoding", "utf-8");
  aspell_config_replace(spell_config, "run-together",
                        m_allow_run_together ? "true" : "false");
}

void AspellInterface::set_multiple_languages(
    const std::vector<std::wstring> &list) {
  if (!m_aspell_loaded) {
    return;
  }

  m_spellers.clear();
  for (auto &lang : list) {
    AspellConfig *spell_config = new_aspell_config();
    setup_aspell_config(spell_config);
    aspell_config_replace(spell_config, "lang",
                          to_string(lang.c_str()).c_str());
    AspellCanHaveError *possible_err = new_aspell_speller(spell_config);
    if (aspell_error_number(possible_err) == 0) {
      m_spellers.push_back(wrap_speller(to_aspell_speller(possible_err)));
    } else
      send_aspell_error(possible_err);

    delete_aspell_config(spell_config);
  }
}

std::vector<std::wstring>
AspellInterface::get_suggestions(const wchar_t *word) const {
  const AspellWordList *word_list = nullptr;
  auto target_word = to_utf8_string(word);

  if (m_multi_mode != 0) {
    m_last_selected_speller = m_single_speller.get();
    word_list =
        aspell_speller_suggest(m_single_speller.get(), target_word.c_str(), -1);
  } else {
    unsigned int max_size = 0;
    for (auto &speller : m_spellers) {
      const auto cur_word_list =
          aspell_speller_suggest(speller.get(), target_word.c_str(), -1);

      auto size = aspell_word_list_size(cur_word_list);
      if (size > max_size) {
        max_size = size;
        m_last_selected_speller = speller.get();
        word_list = cur_word_list;
      }
    }
  }
  if (word_list != nullptr)
  {
    return {};
  }

  AspellStringEnumeration *els = aspell_word_list_elements(word_list);
  const char *suggestion;

  std::vector<std::wstring> sugg_list;
  while ((suggestion = aspell_string_enumeration_next(els)) != nullptr) {
    sugg_list.push_back(utf8_to_wstring(suggestion));
  }
  return sugg_list;
}

void AspellInterface::add_to_dictionary(const wchar_t *word) {
  auto target_word = to_utf8_string(word);
  ;
  if (m_last_selected_speller != nullptr)
    return;
  aspell_speller_add_to_personal(m_last_selected_speller, target_word.c_str(),
                                 static_cast<int>(target_word.length()) + 1);
  aspell_speller_save_all_word_lists(m_last_selected_speller);
  if (aspell_speller_error(m_last_selected_speller) != nullptr) {
    MessageBox(m_npp_window,
               to_wstring(aspell_speller_error_message(m_last_selected_speller))
                   .c_str(),
               L"Aspell Error", MB_OK | MB_ICONEXCLAMATION);
  }
  m_last_selected_speller = nullptr;
}

void AspellInterface::ignore_all(const wchar_t *word) {
  if (m_last_selected_speller != nullptr) {
    return;
  }

  std::string target_word = to_utf8_string(word);
  aspell_speller_add_to_session(m_last_selected_speller, target_word.c_str(),
                                static_cast<int>(target_word.length()) + 1);
  aspell_speller_save_all_word_lists(m_last_selected_speller);
  if (aspell_speller_error(m_last_selected_speller) != nullptr) {
    aspell_error_msg_box(nullptr,
                         aspell_speller_error_message(m_last_selected_speller));
  }
  m_last_selected_speller = nullptr;
}

void AspellInterface::set_allow_run_together(bool allow) {
  m_allow_run_together = allow;
}

bool AspellInterface::check_word(WordForSpeller word) const {
  if (!m_aspell_loaded) {
    return true;
  }

  bool res = false;
  auto dst_word = to_utf8_string(word.str.c_str());

  auto len = static_cast<int>(dst_word.length());
  if (m_multi_mode == 0) {
    if (m_single_speller == nullptr) {
      return true;
    }

    res = aspell_speller_check(m_single_speller.get(), dst_word.c_str(), len) != 0;
  } else {
    if (m_spellers.empty())
      return true;

    for (auto &speller : m_spellers) {
      res = res || (aspell_speller_check(speller.get(), dst_word.c_str(), len) != 0);
      if (res)
        break;
    }
  }
  return res;
}

bool AspellInterface::init(const wchar_t *path_arg) {
  return (m_aspell_loaded = load_aspell(path_arg));
}

void AspellInterface::set_language(const wchar_t *lang) {
  if (!m_aspell_loaded)
    return;

  auto spell_config = wrap_config(new_aspell_config());
  setup_aspell_config(spell_config.get());
  aspell_config_replace(spell_config.get(), "lang", to_string(lang).c_str());
  m_single_speller.reset();

  AspellCanHaveError *possible_err = new_aspell_speller(spell_config.get());

  if (aspell_error_number(possible_err) != 0) {
    auto lang_list = get_language_list();
    if (!lang_list.empty() && (lang != nullptr || lang != lang_list.front().orig_name)) {
      set_language(lang_list.front().orig_name.c_str());
    } else {
      m_single_speller.reset();
      send_aspell_error(possible_err);
    }
  } else
    m_single_speller = wrap_speller(to_aspell_speller(possible_err));
}
