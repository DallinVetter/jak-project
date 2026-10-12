// The line editor behind goalc's REPL. The game runtime never constructs one, but
// common/repl/repl_wrapper.cpp, which wraps it, also holds REPL::find_repl_username(),
// which the game kernel calls, so the wrapper's object is linked and these members must
// exist. A PS5 title has no terminal to edit a line on.
#include "third-party/replxx/include/replxx.hxx"

namespace replxx {
char const* Replxx::input(std::string const&) {
  return nullptr;
}
void Replxx::set_state(State const&) {}
void Replxx::print(char const*, ...) {}
Replxx::ACTION_RESULT Replxx::invoke(ACTION, char32_t) {
  return ACTION_RESULT::BAIL;
}
void Replxx::bind_key(char32_t, key_press_handler_t) {}
void Replxx::history_add(std::string const&) {}
bool Replxx::history_save(std::string const&) {
  return false;
}
bool Replxx::history_load(std::string const&) {
  return false;
}
void Replxx::set_word_break_characters(char const*) {}
void Replxx::set_complete_on_empty(bool) {}
void Replxx::set_indent_multiline(bool) {}
void Replxx::set_max_history_size(int) {}
void Replxx::clear_screen(void) {}
void Replxx::enable_bracketed_paste(void) {}
}  // namespace replxx
