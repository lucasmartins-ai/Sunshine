/**
 * @file src/platform/gamepad_keymap.h
 * @brief Declarations for translating client gamepad state into keyboard keys.
 */
#pragma once

// standard includes
#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <string_view>
#include <vector>

// local includes
#include "src/platform/common.h"

namespace platf::keymap {

  /**
   * @brief Gamepad inputs that can be bound to a keyboard key.
   */
  enum class input_e : std::uint8_t {
    a,  ///< A button.
    b,  ///< B button.
    x,  ///< X button.
    y,  ///< Y button.
    lb,  ///< Left bumper.
    rb,  ///< Right bumper.
    lt,  ///< Left trigger past its press threshold.
    rt,  ///< Right trigger past its press threshold.
    back,  ///< Back or select button.
    start,  ///< Start or menu button.
    home,  ///< Guide or home button.
    l3,  ///< Left stick click.
    r3,  ///< Right stick click.
    dpad_up,  ///< D-pad up.
    dpad_down,  ///< D-pad down.
    dpad_left,  ///< D-pad left.
    dpad_right,  ///< D-pad right.
    ls_up,  ///< Left stick pushed up.
    ls_down,  ///< Left stick pushed down.
    ls_left,  ///< Left stick pushed left.
    ls_right,  ///< Left stick pushed right.
    rs_up,  ///< Right stick pushed up.
    rs_down,  ///< Right stick pushed down.
    rs_left,  ///< Right stick pushed left.
    rs_right,  ///< Right stick pushed right.
    count  ///< Number of bindable inputs.
  };

  constexpr std::size_t INPUT_COUNT = static_cast<std::size_t>(input_e::count);  ///< Number of bindable inputs.

  /**
   * @brief Windows virtual-key code bound to each input; zero leaves the input unbound.
   */
  using keymap_t = std::array<std::uint16_t, INPUT_COUNT>;

  /**
   * @brief Key press or release produced by a gamepad state change.
   */
  struct key_event_t {
    std::uint16_t key_code;  ///< Windows virtual-key code.
    bool release;  ///< Whether the key is released instead of pressed.

    /**
     * @brief Compare two key events field by field.
     *
     * @param other Key event to compare against.
     * @return Whether both events press or release the same key.
     */
    bool operator==(const key_event_t &other) const = default;
  };

  /**
   * @brief Get the built-in bindings: left stick on WASD, D-pad on the arrow keys.
   *
   * @return Default keymap.
   */
  keymap_t default_keymap();

  /**
   * @brief Apply comma-separated `input=key` overrides on top of the default keymap.
   * @details Keys are letters, digits, `f1`-`f12`, named keys such as `space` or `enter`,
   *          `none` to unbind, or a hexadecimal virtual-key code such as `0x41`.
   *
   * @param spec Override list, for example `a=space,b=escape,rs_up=none`.
   * @return Resulting keymap, or empty when an entry is malformed.
   */
  std::optional<keymap_t> parse_keymap(std::string_view spec);

  /**
   * @brief Set the keymap overrides of the application being streamed.
   *
   * @param spec The application's override list, or empty to fall back to the global keymap.
   */
  void set_app_keymap(std::string spec);

  /**
   * @brief Pick the override list for a new keymap gamepad.
   *
   * @param global_spec Global override list from configuration.
   * @return The streamed application's overrides when it has any, otherwise `global_spec`.
   */
  std::string active_keymap_spec(std::string_view global_spec);

  /**
   * @brief Tracks which bound inputs are held and emits key events as the gamepad state changes.
   */
  class mapper_t {
  public:
    /**
     * @brief Create a mapper with every input released.
     *
     * @param keymap Bindings used for every state update.
     */
    explicit mapper_t(const keymap_t &keymap);

    /**
     * @brief Compare a new gamepad state against the held inputs.
     *
     * @param state Latest state reported by the client.
     * @return Presses and releases needed to reach the new state.
     */
    std::vector<key_event_t> update(const gamepad_state_t &state);

    /**
     * @brief Release every key this mapper is holding.
     *
     * @return Releases for the held keys.
     */
    std::vector<key_event_t> release_all();

  private:
    keymap_t keymap;  ///< Bindings used for every state update.
    std::array<bool, INPUT_COUNT> held {};  ///< Whether each input is currently past its press threshold.
    std::array<std::uint8_t, 256> key_holds {};  ///< Held inputs per key, so a key shared by two inputs stays down until both release.
  };

}  // namespace platf::keymap
