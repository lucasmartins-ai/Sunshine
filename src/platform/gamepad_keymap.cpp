/**
 * @file src/platform/gamepad_keymap.cpp
 * @brief Definitions for translating client gamepad state into keyboard keys.
 */
// standard includes
#include <algorithm>
#include <cctype>
#include <charconv>
#include <mutex>
#include <string>
#include <utility>

// lib includes
#include <moonlight-common-c/src/Limelight.h>

// local includes
#include "gamepad_keymap.h"

using namespace std::literals;

namespace platf::keymap {

  namespace {

    constexpr int STICK_PRESS = 16384;  ///< Stick deflection (half of full travel) that presses a direction.
    constexpr int STICK_RELEASE = 12288;  ///< Lower deflection that releases it, so a stick resting near the threshold does not chatter.
    constexpr int TRIGGER_PRESS = 128;  ///< Trigger travel that presses its key.
    constexpr int TRIGGER_RELEASE = 96;  ///< Lower trigger travel that releases it.

    /**
     * @brief Names accepted for each input, in `input_e` order.
     */
    constexpr std::array<std::string_view, INPUT_COUNT> INPUT_NAMES {
      "a"sv,
      "b"sv,
      "x"sv,
      "y"sv,
      "lb"sv,
      "rb"sv,
      "lt"sv,
      "rt"sv,
      "back"sv,
      "start"sv,
      "home"sv,
      "l3"sv,
      "r3"sv,
      "dpad_up"sv,
      "dpad_down"sv,
      "dpad_left"sv,
      "dpad_right"sv,
      "ls_up"sv,
      "ls_down"sv,
      "ls_left"sv,
      "ls_right"sv,
      "rs_up"sv,
      "rs_down"sv,
      "rs_left"sv,
      "rs_right"sv,
    };

    /**
     * @brief Moonlight button flag for each digital input, in `input_e` order; zero for analog inputs.
     */
    constexpr std::array<std::uint32_t, INPUT_COUNT> BUTTON_FLAGS {
      A_FLAG,
      B_FLAG,
      X_FLAG,
      Y_FLAG,
      LB_FLAG,
      RB_FLAG,
      0,
      0,
      BACK_FLAG,
      PLAY_FLAG,
      SPECIAL_FLAG,
      LS_CLK_FLAG,
      RS_CLK_FLAG,
      UP_FLAG,
      DOWN_FLAG,
      LEFT_FLAG,
      RIGHT_FLAG,
    };

    /**
     * @brief Named keys accepted in a keymap, as Windows virtual-key codes.
     */
    constexpr std::array<std::pair<std::string_view, std::uint16_t>, 17> NAMED_KEYS {{
      {"space"sv, 0x20},
      {"enter"sv, 0x0D},
      {"return"sv, 0x0D},
      {"escape"sv, 0x1B},
      {"esc"sv, 0x1B},
      {"tab"sv, 0x09},
      {"backspace"sv, 0x08},
      {"shift"sv, 0xA0},
      {"rshift"sv, 0xA1},
      {"ctrl"sv, 0xA2},
      {"rctrl"sv, 0xA3},
      {"alt"sv, 0xA4},
      {"ralt"sv, 0xA5},
      {"up"sv, 0x26},
      {"down"sv, 0x28},
      {"left"sv, 0x25},
      {"right"sv, 0x27},
    }};

    /**
     * @brief Bind one input in a keymap.
     *
     * @param keymap Keymap to modify.
     * @param input Input to bind.
     * @param key_code Windows virtual-key code to press for the input.
     */
    constexpr void bind_key(keymap_t &keymap, input_e input, std::uint16_t key_code) {
      keymap[static_cast<std::size_t>(input)] = key_code;
    }

    /**
     * @brief Decide whether a value is past its threshold, using the lower one while already held.
     *
     * @param value Axis or trigger value, positive in the bound direction.
     * @param held Whether the input is currently held.
     * @param press Threshold that presses a released input.
     * @param release Threshold below which a held input releases.
     * @return Whether the input should be held.
     */
    bool past(int value, bool held, int press, int release) {
      return value >= (held ? release : press);
    }

    /**
     * @brief Decide whether an input is active in a gamepad state.
     *
     * @param input Input to test.
     * @param state Latest gamepad state.
     * @param held Whether the input is currently held.
     * @return Whether the input should be held.
     */
    bool is_active(input_e input, const gamepad_state_t &state, bool held) {
      const auto index = static_cast<std::size_t>(input);
      if (BUTTON_FLAGS[index] != 0) {
        return (state.buttonFlags & BUTTON_FLAGS[index]) != 0;
      }

      switch (input) {
        case input_e::lt:
          return past(state.lt, held, TRIGGER_PRESS, TRIGGER_RELEASE);
        case input_e::rt:
          return past(state.rt, held, TRIGGER_PRESS, TRIGGER_RELEASE);
        case input_e::ls_up:
          return past(state.lsY, held, STICK_PRESS, STICK_RELEASE);
        case input_e::ls_down:
          return past(-static_cast<int>(state.lsY), held, STICK_PRESS, STICK_RELEASE);
        case input_e::ls_left:
          return past(-static_cast<int>(state.lsX), held, STICK_PRESS, STICK_RELEASE);
        case input_e::ls_right:
          return past(state.lsX, held, STICK_PRESS, STICK_RELEASE);
        case input_e::rs_up:
          return past(state.rsY, held, STICK_PRESS, STICK_RELEASE);
        case input_e::rs_down:
          return past(-static_cast<int>(state.rsY), held, STICK_PRESS, STICK_RELEASE);
        case input_e::rs_left:
          return past(-static_cast<int>(state.rsX), held, STICK_PRESS, STICK_RELEASE);
        case input_e::rs_right:
          return past(state.rsX, held, STICK_PRESS, STICK_RELEASE);
        default:
          return false;
      }
    }

    /**
     * @brief Trim surrounding whitespace and lowercase a token.
     *
     * @param token Raw text from a keymap entry.
     * @return Trimmed lowercase token.
     */
    std::string normalize(std::string_view token) {
      const auto first = token.find_first_not_of(" \t"sv);
      if (first == std::string_view::npos) {
        return {};
      }
      token = token.substr(first, token.find_last_not_of(" \t"sv) - first + 1);

      std::string out {token};
      std::ranges::transform(out, out.begin(), [](unsigned char c) {
        return static_cast<char>(std::tolower(c));
      });
      return out;
    }

    /**
     * @brief Resolve a key name to a Windows virtual-key code.
     *
     * @param name Normalized key name.
     * @return Virtual-key code, zero for `none`, or empty when the name is unknown.
     */
    std::optional<std::uint16_t> parse_key(std::string_view name) {
      if (name == "none"sv) {
        return 0;
      }
      if (name.size() == 1 && std::isalpha(static_cast<unsigned char>(name[0]))) {
        return static_cast<std::uint16_t>(std::toupper(static_cast<unsigned char>(name[0])));
      }
      if (name.size() == 1 && std::isdigit(static_cast<unsigned char>(name[0]))) {
        return static_cast<std::uint16_t>(name[0]);
      }
      if (name.size() >= 2 && name.size() <= 3 && name[0] == 'f') {
        int number = 0;
        const auto [end, error] = std::from_chars(name.data() + 1, name.data() + name.size(), number);
        if (error == std::errc {} && end == name.data() + name.size() && number >= 1 && number <= 12) {
          return static_cast<std::uint16_t>(0x70 + number - 1);
        }
      }
      if (name.starts_with("0x"sv) && name.size() > 2) {
        unsigned int code = 0;
        const auto [end, error] = std::from_chars(name.data() + 2, name.data() + name.size(), code, 16);
        if (error == std::errc {} && end == name.data() + name.size() && code > 0 && code <= 0xFE) {
          return static_cast<std::uint16_t>(code);
        }
      }
      for (const auto &[key_name, code] : NAMED_KEYS) {
        if (name == key_name) {
          return code;
        }
      }
      return std::nullopt;
    }

  }  // namespace

  keymap_t default_keymap() {
    keymap_t keymap {};
    bind_key(keymap, input_e::a, 0x20);  // space
    bind_key(keymap, input_e::b, 'F');
    bind_key(keymap, input_e::x, '1');
    bind_key(keymap, input_e::y, '2');
    bind_key(keymap, input_e::lb, '3');
    bind_key(keymap, input_e::rb, 'C');
    bind_key(keymap, input_e::lt, 'Q');
    bind_key(keymap, input_e::rt, 'E');
    bind_key(keymap, input_e::back, 0x1B);  // escape
    bind_key(keymap, input_e::start, 0x0D);  // enter
    bind_key(keymap, input_e::l3, 0xA0);  // left shift
    bind_key(keymap, input_e::dpad_up, 0x26);
    bind_key(keymap, input_e::dpad_down, 0x28);
    bind_key(keymap, input_e::dpad_left, 0x25);
    bind_key(keymap, input_e::dpad_right, 0x27);
    bind_key(keymap, input_e::ls_up, 'W');
    bind_key(keymap, input_e::ls_down, 'S');
    bind_key(keymap, input_e::ls_left, 'A');
    bind_key(keymap, input_e::ls_right, 'D');
    bind_key(keymap, input_e::rs_up, 'I');
    bind_key(keymap, input_e::rs_down, 'K');
    bind_key(keymap, input_e::rs_left, 'J');
    bind_key(keymap, input_e::rs_right, 'L');
    return keymap;
  }

  std::optional<keymap_t> parse_keymap(std::string_view spec) {
    auto keymap = default_keymap();
    while (!spec.empty()) {
      const auto comma = spec.find(',');
      const auto entry = spec.substr(0, comma);
      spec = comma == std::string_view::npos ? std::string_view {} : spec.substr(comma + 1);

      if (normalize(entry).empty()) {
        continue;
      }
      const auto equals = entry.find('=');
      if (equals == std::string_view::npos) {
        return std::nullopt;
      }

      const auto input_name = normalize(entry.substr(0, equals));
      const auto input = std::ranges::find(INPUT_NAMES, input_name);
      const auto key_code = parse_key(normalize(entry.substr(equals + 1)));
      if (input == INPUT_NAMES.end() || !key_code) {
        return std::nullopt;
      }
      keymap[static_cast<std::size_t>(input - INPUT_NAMES.begin())] = *key_code;
    }
    return keymap;
  }

  namespace {

    std::mutex app_keymap_mutex;  ///< Guards app_keymap: apps launch on the HTTP thread, gamepads arrive on the input thread.
    std::string app_keymap;  ///< Override list of the application being streamed; empty means the global keymap.

  }  // namespace

  void set_app_keymap(std::string spec) {
    std::lock_guard lock {app_keymap_mutex};
    app_keymap = std::move(spec);
  }

  std::string active_keymap_spec(std::string_view global_spec) {
    std::lock_guard lock {app_keymap_mutex};
    return app_keymap.empty() ? std::string {global_spec} : app_keymap;
  }

  mapper_t::mapper_t(const keymap_t &keymap):
      keymap {keymap} {}

  std::vector<key_event_t> mapper_t::update(const gamepad_state_t &state) {
    // Settle every input first, so a key passed from one input to another in the same state stays down.
    std::array<std::uint8_t, 256> next_holds {};
    for (std::size_t index = 0; index < INPUT_COUNT; ++index) {
      held[index] = is_active(static_cast<input_e>(index), state, held[index]);
      if (held[index] && keymap[index] != 0) {
        ++next_holds[keymap[index] & 0xFF];
      }
    }

    std::vector<key_event_t> events;
    for (std::size_t index = 0; index < INPUT_COUNT; ++index) {
      const auto key_code = keymap[index];
      auto &holds = key_holds[key_code & 0xFF];
      const auto next = next_holds[key_code & 0xFF];
      if (key_code != 0 && (holds > 0) != (next > 0)) {
        events.push_back({key_code, next == 0});
      }
      holds = next;
    }
    return events;
  }

  std::vector<key_event_t> mapper_t::release_all() {
    return update(gamepad_state_t {});
  }

}  // namespace platf::keymap
