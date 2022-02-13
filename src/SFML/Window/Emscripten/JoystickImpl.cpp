////////////////////////////////////////////////////////////
//
// SFML - Simple and Fast Multimedia Library
// Copyright (C) 2007-2021 Laurent Gomila (laurent@sfml-dev.org)
//
// This software is provided 'as-is', without any express or implied warranty.
// In no event will the authors be held liable for any damages arising from the use of this software.
//
// Permission is granted to anyone to use this software for any purpose,
// including commercial applications, and to alter it and redistribute it freely,
// subject to the following restrictions:
//
// 1. The origin of this software must not be misrepresented;
//    you must not claim that you wrote the original software.
//    If you use this software in a product, an acknowledgment
//    in the product documentation would be appreciated but is not required.
//
// 2. Altered source versions must be plainly marked as such,
//    and must not be misrepresented as being the original software.
//
// 3. This notice may not be removed or altered from any source distribution.
//
////////////////////////////////////////////////////////////

////////////////////////////////////////////////////////////
// Headers
////////////////////////////////////////////////////////////

#include <ostream>
#include <SFML/Window/JoystickImpl.hpp>
#include <SFML/System/Err.hpp>

namespace sf
{
namespace priv
{

EM_BOOL JoystickImpl::joystick_connect_disconnect_callback(int eventType, const EmscriptenGamepadEvent *e, void *userData) {
    (void) e;
    (void) userData;
    (void) eventType;

    return 0;
}

////////////////////////////////////////////////////////////
JoystickImpl::JoystickImpl() { }


////////////////////////////////////////////////////////////
void JoystickImpl::initialize() {
    EMSCRIPTEN_RESULT res = emscripten_sample_gamepad_data();
    if (res != EMSCRIPTEN_RESULT_SUCCESS) {
        sf::err() << "Emscripten joystick support not available" << std::endl;
        return;
    }

    res = emscripten_set_gamepadconnected_callback(nullptr, 1, joystick_connect_disconnect_callback);
    if (res != EMSCRIPTEN_RESULT_SUCCESS) {
        sf::err() << "Failed to register joystick connection callback, joystick connections won't be notified" << std::endl;
        return;
    }
    res = emscripten_set_gamepaddisconnected_callback(nullptr, 1, joystick_connect_disconnect_callback);
    if (res != EMSCRIPTEN_RESULT_SUCCESS) {
        sf::err() << "Failed to register joystick disconnection callback, joystick disconnections won't be notified" << std::endl;
        return;
    }
}


////////////////////////////////////////////////////////////
void JoystickImpl::cleanup() {
    // Unsubscribe from events
    emscripten_set_gamepadconnected_callback(nullptr, 1, nullptr);
    emscripten_set_gamepaddisconnected_callback(nullptr, 1, nullptr);
}


////////////////////////////////////////////////////////////
bool JoystickImpl::isConnected(unsigned int index)
{
    (void) index;

    /*
    // See if we can skip scanning if udev monitor is available
    if (!udevMonitor)
    {
        // udev monitor is not available, perform a scan every query
        updatePluggedList();
    }
    else if (hasMonitorEvent())
    {
        // Check if new joysticks were added/removed since last update
        udev_device* udevDevice = udev_monitor_receive_device(udevMonitor);

        // If we can get the specific device, we check that,
        // otherwise just do a full scan if udevDevice == NULL
        updatePluggedList(udevDevice);

        if (udevDevice)
            udev_device_unref(udevDevice);
    }

    if (index >= joystickList.size())
        return false;

    // Then check if the joystick is connected
    return joystickList[index].plugged;
    */

    return false;
}

////////////////////////////////////////////////////////////
bool JoystickImpl::open(unsigned int index)
{
    (void) index;

    /*
    if (index >= joystickList.size())
        return false;

    if (joystickList[index].plugged)
    {
        std::string devnode = joystickList[index].deviceNode;

        // Open the joystick's file descriptor (read-only and non-blocking)
        m_file = ::open(devnode.c_str(), O_RDONLY | O_NONBLOCK);
        if (m_file >= 0)
        {
            // Retrieve the axes mapping
            ioctl(m_file, JSIOCGAXMAP, m_mapping);

            // Get info
            m_identification.name = getJoystickName(index);

            if (udevContext)
            {
                m_identification.vendorId  = getJoystickVendorId(index);
                m_identification.productId = getJoystickProductId(index);
            }

            // Reset the joystick state
            m_state = JoystickState();

            return true;
        }
        else
        {
            err() << "Failed to open joystick " << devnode << ": " << errno << std::endl;
        }
    }
    */

    return false;
}


////////////////////////////////////////////////////////////
void JoystickImpl::close()
{
    /*
    ::close(m_file);
    m_file = -1;
    */
}


////////////////////////////////////////////////////////////
JoystickCaps JoystickImpl::getCapabilities() const
{

    JoystickCaps caps;
    /*

    if (m_file < 0)
        return caps;

    // Get the number of buttons
    char buttonCount;
    ioctl(m_file, JSIOCGBUTTONS, &buttonCount);
    caps.buttonCount = buttonCount;
    if (caps.buttonCount > Joystick::ButtonCount)
        caps.buttonCount = Joystick::ButtonCount;

    // Get the supported axes
    char axesCount;
    ioctl(m_file, JSIOCGAXES, &axesCount);
    for (int i = 0; i < axesCount; ++i)
    {
        switch (m_mapping[i])
        {
            case ABS_X:        caps.axes[Joystick::X]    = true; break;
            case ABS_Y:        caps.axes[Joystick::Y]    = true; break;
            case ABS_Z:
            case ABS_THROTTLE: caps.axes[Joystick::Z]    = true; break;
            case ABS_RZ:
            case ABS_RUDDER:   caps.axes[Joystick::R]    = true; break;
            case ABS_RX:       caps.axes[Joystick::U]    = true; break;
            case ABS_RY:       caps.axes[Joystick::V]    = true; break;
            case ABS_HAT0X:    caps.axes[Joystick::PovX] = true; break;
            case ABS_HAT0Y:    caps.axes[Joystick::PovY] = true; break;
            default:           break;
        }
    }
    */

    return caps;
}


////////////////////////////////////////////////////////////
Joystick::Identification JoystickImpl::getIdentification() const
{
    return m_identification;
}


////////////////////////////////////////////////////////////
JoystickState JoystickImpl::JoystickImpl::update()
{
    /*
    if (m_file < 0)
    {
        m_state = JoystickState();
        return m_state;
    }

    // pop events from the joystick file
    js_event joyState;
    int result = read(m_file, &joyState, sizeof(joyState));
    while (result > 0)
    {
        switch (joyState.type & ~JS_EVENT_INIT)
        {
            // An axis was moved
            case JS_EVENT_AXIS:
            {
                float value = joyState.value * 100.f / 32767.f;

                if (joyState.number < ABS_MAX + 1)
                {
                    switch (m_mapping[joyState.number])
                    {
                        case ABS_X:        m_state.axes[Joystick::X]    = value; break;
                        case ABS_Y:        m_state.axes[Joystick::Y]    = value; break;
                        case ABS_Z:
                        case ABS_THROTTLE: m_state.axes[Joystick::Z]    = value; break;
                        case ABS_RZ:
                        case ABS_RUDDER:   m_state.axes[Joystick::R]    = value; break;
                        case ABS_RX:       m_state.axes[Joystick::U]    = value; break;
                        case ABS_RY:       m_state.axes[Joystick::V]    = value; break;
                        case ABS_HAT0X:    m_state.axes[Joystick::PovX] = value; break;
                        case ABS_HAT0Y:    m_state.axes[Joystick::PovY] = value; break;
                        default:           break;
                    }
                }
                break;
            }

            // A button was pressed
            case JS_EVENT_BUTTON:
            {
                if (joyState.number < Joystick::ButtonCount)
                    m_state.buttons[joyState.number] = (joyState.value != 0);
                break;
            }
        }

        result = read(m_file, &joyState, sizeof(joyState));
    }

    // Check the connection state of the joystick
    // read() returns -1 and errno != EGAIN if it's no longer connected
    // We need to check the result of read() as well, since errno could
    // have been previously set by some other function call that failed
    // result can be either negative or 0 at this point
    // If result is 0, assume the joystick is still connected
    // If result is negative, check errno and disconnect if it is not EAGAIN
    m_state.connected = (!result || (errno == EAGAIN));

    */

    return m_state;
}

} // namespace priv

} // namespace sf
