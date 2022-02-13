////////////////////////////////////////////////////////////
//
// SFML - Simple and Fast Multimedia Library
// Copyright (C) 2007-2022 Laurent Gomila (laurent@sfml-dev.org)
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
#include <SFML/Window/Emscripten/WindowImplEmscripten.hpp>
#include <SFML/Window/Unix/ClipboardImpl.hpp>
#include <SFML/Window/Unix/Display.hpp>
#include <SFML/Window/Unix/InputImpl.hpp>
#include <SFML/System/String.hpp>
#include <SFML/System/Utf.hpp>
#include <SFML/System/Err.hpp>
#include <SFML/System/Sleep.hpp>
#include <SFML/System/Time.hpp>

#include <X11/Xlibint.h>
#undef min // Defined by `Xlibint.h`, conflicts with standard headers
#undef max // Defined by `Xlibint.h`, conflicts with standard headers

#include <X11/Xutil.h>
#include <X11/Xatom.h>
#include <X11/keysym.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>
#include <libgen.h>
#include <fcntl.h>
#include <algorithm>
#include <vector>
#include <string>
#include <cstring>
#include <cassert>
#include <mutex>

#include <SFML/Window/EglContext.hpp>

using ContextType = sf::priv::EglContext;

////////////////////////////////////////////////////////////
// Private data
////////////////////////////////////////////////////////////
namespace
{
    // A nested named namespace is used here to allow unity builds of SFML.
    namespace WindowsImplEmscriptenImpl
    {
        sf::priv::WindowImplEmscripten*              fullscreenWindow = nullptr;
        std::vector<sf::priv::WindowImplEmscripten*> allWindows;
        std::recursive_mutex                  allWindowsMutex;
        sf::String                            windowManagerName;

        sf::String                            wmAbsPosGood[] = { "Enlightenment", "FVWM", "i3" };

    /*
        static const unsigned long            eventMask = FocusChangeMask      | ButtonPressMask     |
                                                        ButtonReleaseMask    | ButtonMotionMask    |
                                                        PointerMotionMask    | KeyPressMask        |
                                                        KeyReleaseMask       | StructureNotifyMask |
                                                        EnterWindowMask      | LeaveWindowMask     |
                                                        VisibilityChangeMask | PropertyChangeMask;
    */

        static const unsigned int             maxTrialsCount = 5;

        // Predicate we use to find key repeat events in processEvent
        struct KeyRepeatFinder
        {
            KeyRepeatFinder(unsigned int initalKeycode, Time initialTime) : keycode(initalKeycode), time(initialTime) {}

            // Predicate operator that checks event type, keycode and timestamp
            bool operator()(const XEvent& event)
            {
                return ((event.type == KeyPress) && (event.xkey.keycode == keycode) && (event.xkey.time - time < 2));
            }

            unsigned int keycode;
            Time time;
        };

        // Get the parent window.
        ::Window getParentWindow(::Display* disp, ::Window win)
        {
            ::Window root, parent;
            ::Window* children = nullptr;
            unsigned int numChildren;

            XQueryTree(disp, win, &root, &parent, &children, &numChildren);

            // Children information is not used, so must be freed.
            if (children != nullptr)
                XFree(children);

            return parent;
        }

        // Get the Frame Extents from EWMH WMs that support it.
        bool getEWMHFrameExtents(::Display* disp, ::Window win,
            long& xFrameExtent, long& yFrameExtent)
        {
            if (false)
                return false;

            Atom frameExtents = sf::priv::getAtom("_NET_FRAME_EXTENTS", true);

            if (frameExtents == None)
                return false;

            bool gotFrameExtents = false;
            Atom actualType;
            int actualFormat;
            unsigned long numItems;
            unsigned long numBytesLeft;
            unsigned char* data = nullptr;

            int result = XGetWindowProperty(disp,
                                            win,
                                            frameExtents,
                                            0,
                                            4,
                                            False,
                                            XA_CARDINAL,
                                            &actualType,
                                            &actualFormat,
                                            &numItems,
                                            &numBytesLeft,
                                            &data);

            if ((result == Success) && (actualType == XA_CARDINAL) &&
                (actualFormat == 32) && (numItems == 4) && (numBytesLeft == 0) &&
                (data != nullptr))
            {
                gotFrameExtents = true;

                #pragma GCC diagnostic push
                #pragma GCC diagnostic ignored "-Wcast-align"
                long* extents = reinterpret_cast<long*>(data);
                #pragma GCC diagnostic pop

                xFrameExtent = extents[0]; // Left.
                yFrameExtent = extents[2]; // Top.
            }

            // Always free data.
            if (data != nullptr)
                XFree(data);

            return gotFrameExtents;
        }

        // Check if the current WM is in the list of good WMs that provide
        // a correct absolute position for the window when queried.
        bool isWMAbsolutePositionGood()
        {
            // This can only work with EWMH, to get the name.
            if (false)
                return false;

            for (const sf::String& name : wmAbsPosGood)
            {
                if (name == windowManagerName)
                    return true;
            }

            return false;
        }

        sf::Keyboard::Key keysymToSF(KeySym symbol)
        {
            switch (symbol)
            {
                case XK_Shift_L:      return sf::Keyboard::LShift;
                case XK_Shift_R:      return sf::Keyboard::RShift;
                case XK_Control_L:    return sf::Keyboard::LControl;
                case XK_Control_R:    return sf::Keyboard::RControl;
                case XK_Alt_L:        return sf::Keyboard::LAlt;
                case XK_Alt_R:        return sf::Keyboard::RAlt;
                case XK_Super_L:      return sf::Keyboard::LSystem;
                case XK_Super_R:      return sf::Keyboard::RSystem;
                case XK_Menu:         return sf::Keyboard::Menu;
                case XK_Escape:       return sf::Keyboard::Escape;
                case XK_semicolon:    return sf::Keyboard::Semicolon;
                case XK_slash:        return sf::Keyboard::Slash;
                case XK_equal:        return sf::Keyboard::Equal;
                case XK_minus:        return sf::Keyboard::Hyphen;
                case XK_bracketleft:  return sf::Keyboard::LBracket;
                case XK_bracketright: return sf::Keyboard::RBracket;
                case XK_comma:        return sf::Keyboard::Comma;
                case XK_period:       return sf::Keyboard::Period;
                case XK_apostrophe:   return sf::Keyboard::Quote;
                case XK_backslash:    return sf::Keyboard::Backslash;
                case XK_grave:        return sf::Keyboard::Tilde;
                case XK_space:        return sf::Keyboard::Space;
                case XK_Return:       return sf::Keyboard::Enter;
                case XK_KP_Enter:     return sf::Keyboard::Enter;
                case XK_BackSpace:    return sf::Keyboard::Backspace;
                case XK_Tab:          return sf::Keyboard::Tab;
                case XK_Prior:        return sf::Keyboard::PageUp;
                case XK_Next:         return sf::Keyboard::PageDown;
                case XK_End:          return sf::Keyboard::End;
                case XK_Home:         return sf::Keyboard::Home;
                case XK_Insert:       return sf::Keyboard::Insert;
                case XK_Delete:       return sf::Keyboard::Delete;
                case XK_KP_Add:       return sf::Keyboard::Add;
                case XK_KP_Subtract:  return sf::Keyboard::Subtract;
                case XK_KP_Multiply:  return sf::Keyboard::Multiply;
                case XK_KP_Divide:    return sf::Keyboard::Divide;
                case XK_Pause:        return sf::Keyboard::Pause;
                case XK_F1:           return sf::Keyboard::F1;
                case XK_F2:           return sf::Keyboard::F2;
                case XK_F3:           return sf::Keyboard::F3;
                case XK_F4:           return sf::Keyboard::F4;
                case XK_F5:           return sf::Keyboard::F5;
                case XK_F6:           return sf::Keyboard::F6;
                case XK_F7:           return sf::Keyboard::F7;
                case XK_F8:           return sf::Keyboard::F8;
                case XK_F9:           return sf::Keyboard::F9;
                case XK_F10:          return sf::Keyboard::F10;
                case XK_F11:          return sf::Keyboard::F11;
                case XK_F12:          return sf::Keyboard::F12;
                case XK_F13:          return sf::Keyboard::F13;
                case XK_F14:          return sf::Keyboard::F14;
                case XK_F15:          return sf::Keyboard::F15;
                case XK_Left:         return sf::Keyboard::Left;
                case XK_Right:        return sf::Keyboard::Right;
                case XK_Up:           return sf::Keyboard::Up;
                case XK_Down:         return sf::Keyboard::Down;
                case XK_KP_Insert:    return sf::Keyboard::Numpad0;
                case XK_KP_End:       return sf::Keyboard::Numpad1;
                case XK_KP_Down:      return sf::Keyboard::Numpad2;
                case XK_KP_Page_Down: return sf::Keyboard::Numpad3;
                case XK_KP_Left:      return sf::Keyboard::Numpad4;
                case XK_KP_Begin:     return sf::Keyboard::Numpad5;
                case XK_KP_Right:     return sf::Keyboard::Numpad6;
                case XK_KP_Home:      return sf::Keyboard::Numpad7;
                case XK_KP_Up:        return sf::Keyboard::Numpad8;
                case XK_KP_Page_Up:   return sf::Keyboard::Numpad9;
                case XK_a:            return sf::Keyboard::A;
                case XK_b:            return sf::Keyboard::B;
                case XK_c:            return sf::Keyboard::C;
                case XK_d:            return sf::Keyboard::D;
                case XK_e:            return sf::Keyboard::E;
                case XK_f:            return sf::Keyboard::F;
                case XK_g:            return sf::Keyboard::G;
                case XK_h:            return sf::Keyboard::H;
                case XK_i:            return sf::Keyboard::I;
                case XK_j:            return sf::Keyboard::J;
                case XK_k:            return sf::Keyboard::K;
                case XK_l:            return sf::Keyboard::L;
                case XK_m:            return sf::Keyboard::M;
                case XK_n:            return sf::Keyboard::N;
                case XK_o:            return sf::Keyboard::O;
                case XK_p:            return sf::Keyboard::P;
                case XK_q:            return sf::Keyboard::Q;
                case XK_r:            return sf::Keyboard::R;
                case XK_s:            return sf::Keyboard::S;
                case XK_t:            return sf::Keyboard::T;
                case XK_u:            return sf::Keyboard::U;
                case XK_v:            return sf::Keyboard::V;
                case XK_w:            return sf::Keyboard::W;
                case XK_x:            return sf::Keyboard::X;
                case XK_y:            return sf::Keyboard::Y;
                case XK_z:            return sf::Keyboard::Z;
                case XK_0:            return sf::Keyboard::Num0;
                case XK_1:            return sf::Keyboard::Num1;
                case XK_2:            return sf::Keyboard::Num2;
                case XK_3:            return sf::Keyboard::Num3;
                case XK_4:            return sf::Keyboard::Num4;
                case XK_5:            return sf::Keyboard::Num5;
                case XK_6:            return sf::Keyboard::Num6;
                case XK_7:            return sf::Keyboard::Num7;
                case XK_8:            return sf::Keyboard::Num8;
                case XK_9:            return sf::Keyboard::Num9;
            }

            return sf::Keyboard::Unknown;
        }
    }
}


namespace sf
{
namespace priv
{

////////////////////////////////////////////////////////////
WindowImplEmscripten::WindowImplEmscripten(VideoMode mode, const String& title, unsigned long style, const ContextSettings& /*settings*/) :
m_window         (0),
m_screen         (0),
m_inputMethod    (nullptr),
m_inputContext   (nullptr),
m_isExternal     (false),
m_keyRepeat      (true),
m_previousSize   (-1, -1),
m_fullscreen     ((style & Style::Fullscreen) != 0),
m_cursorGrabbed  (m_fullscreen),
m_windowMapped   (false),
m_iconPixmap     (0),
m_iconMaskPixmap (0),
m_lastInputTime  (0)
{
    using namespace WindowsImplEmscriptenImpl;

    glfwInit();

    glfwWindowHint(GLFW_CLIENT_API, GLFW_OPENGL_ES_API);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 1);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 0);
    glfwWindowHint(GLFW_CONTEXT_CREATION_API, GLFW_EGL_CONTEXT_API);

    m_glfwWindow = glfwCreateWindow(800, 600, title.toAnsiString().c_str(), nullptr, nullptr);
    glfwMakeContextCurrent(m_glfwWindow);

    int gles_version = gladLoadEGL(EGL_NO_DISPLAY, glfwGetProcAddress);
    printf("GLES %d.%d\n", GLAD_VERSION_MAJOR(gles_version), GLAD_VERSION_MINOR(gles_version));

    if (!m_window)
    {
        err() << "Failed to create window" << std::endl;
        return;
    }

    // The class name identifies a class of windows that
    // "are of the same type". We simply use the initial window name as
    // the class name.
    //std::string ansiTitle = title.toAnsiString();
    //std::vector<char> windowClass(ansiTitle.size() + 1, 0);
    //std::copy(ansiTitle.begin(), ansiTitle.end(), windowClass.begin());

    // Set the window's name
    //setTitle(title);

    // Do some common initializations
    initialize();

    // Set fullscreen video mode and switch to fullscreen if necessary
    if (m_fullscreen)
    {
        // Disable hint for min and max size,
        // otherwise some windows managers will not remove window decorations
        XSizeHints *sizeHints = XAllocSizeHints();
        long flags = 0;
        XGetWMNormalHints(m_display, m_window, sizeHints, &flags);
        sizeHints->flags &= ~(PMinSize | PMaxSize);
        XSetWMNormalHints(m_display, m_window, sizeHints);
        XFree(sizeHints);

        setVideoMode(mode);
        switchToFullscreen();
    }
}


////////////////////////////////////////////////////////////
WindowImplEmscripten::~WindowImplEmscripten()
{
    using namespace WindowsImplEmscriptenImpl;

    // Cleanup graphical resources
    cleanup();

    // Destroy icon pixmap
    if (m_iconPixmap)
        XFreePixmap(m_display, m_iconPixmap);

    // Destroy icon mask pixmap
    if (m_iconMaskPixmap)
        XFreePixmap(m_display, m_iconMaskPixmap);

    // Destroy the input context
    if (m_inputContext)
        XDestroyIC(m_inputContext);

    // Destroy the window
    if (m_window && !m_isExternal)
    {
        XDestroyWindow(m_display, m_window);
        XFlush(m_display);
    }

    // Close the input method
    if (m_inputMethod)
        CloseXIM(m_inputMethod);

    // Close the connection with the X server
    CloseDisplay(m_display);

    // Remove this window from the global list of windows (required for focus request)
    std::scoped_lock lock(allWindowsMutex);
    allWindows.erase(std::find(allWindows.begin(), allWindows.end(), this));
}


////////////////////////////////////////////////////////////
WindowHandle WindowImplEmscripten::getSystemHandle() const
{
    return m_window;
}


////////////////////////////////////////////////////////////
void WindowImplEmscripten::processEvents()
{
    using namespace WindowsImplEmscriptenImpl;

    XEvent event;

    // Pick out the events that are interesting for this window
    //while (XCheckIfEvent(m_display, &event, &checkEvent, reinterpret_cast<XPointer>(m_window)))
    //    m_events.push_back(event);

    // Handle the events for this window that we just picked out
    while (!m_events.empty())
    {
        event = m_events.front();
        m_events.pop_front();
        processEvent(event);
    }
}


////////////////////////////////////////////////////////////
Vector2i WindowImplEmscripten::getPosition() const
{
    using namespace WindowsImplEmscriptenImpl;

    // Get absolute position of our window relative to root window. This
    // takes into account all information that X11 has, including X11
    // border widths and any decorations. It corresponds to where the
    // window actually is, but not necessarily to where we told it to
    // go using setPosition() and XMoveWindow(). To have the two match
    // as expected, we may have to subtract decorations and borders.
    ::Window child;
    int xAbsRelToRoot, yAbsRelToRoot;

    XTranslateCoordinates(m_display, m_window, DefaultRootWindow(m_display),
        0, 0, &xAbsRelToRoot, &yAbsRelToRoot, &child);

    // CASE 1: some rare WMs actually put the window exactly where we tell
    // it to, even with decorations and such, which get shifted back.
    // In these rare cases, we can use the absolute value directly.
    if (isWMAbsolutePositionGood())
        return Vector2i(xAbsRelToRoot, yAbsRelToRoot);

    // CASE 2: most modern WMs support EWMH and can define _NET_FRAME_EXTENTS
    // with the exact frame size to subtract, so if present, we prefer it and
    // query it first. According to spec, this already includes any borders.
    long xFrameExtent, yFrameExtent;

    if (getEWMHFrameExtents(m_display, m_window, xFrameExtent, yFrameExtent))
    {
        // Get final X/Y coordinates: subtract EWMH frame extents from
        // absolute window position.
        return Vector2i((xAbsRelToRoot - static_cast<int>(xFrameExtent)), (yAbsRelToRoot - static_cast<int>(yFrameExtent)));
    }

    // CASE 3: EWMH frame extents were not available, use geometry.
    // We climb back up to the window before the root and use its
    // geometry information to extract X/Y position. This because
    // re-parenting WMs may re-parent the window multiple times, so
    // we'd have to climb up to the furthest ancestor and sum the
    // relative differences and borders anyway; and doing that to
    // subtract those values from the absolute coordinates of the
    // window is equivalent to going up the tree and asking the
    // furthest ancestor what it's relative distance to the root is.
    // So we use that approach because it's simpler.
    // This approach assumes that any window between the root and
    // our window is part of decorations/borders in some way. This
    // seems to hold true for most reasonable WM implementations.
    ::Window ancestor = m_window;
    ::Window root = DefaultRootWindow(m_display);

    while (getParentWindow(m_display, ancestor) != root)
    {
        // Next window up (parent window).
        ancestor = getParentWindow(m_display, ancestor);
    }

    // Get final X/Y coordinates: take the relative position to
    // the root of the furthest ancestor window.
    int xRelToRoot, yRelToRoot;
    unsigned int width, height, borderWidth, depth;

    XGetGeometry(m_display, ancestor, &root, &xRelToRoot, &yRelToRoot,
        &width, &height, &borderWidth, &depth);

    return Vector2i(xRelToRoot, yRelToRoot);
}


////////////////////////////////////////////////////////////
void WindowImplEmscripten::setPosition(const Vector2i& position)
{
    XMoveWindow(m_display, m_window, position.x, position.y);
    XFlush(m_display);
}


////////////////////////////////////////////////////////////
Vector2u WindowImplEmscripten::getSize() const
{
    Int32 width, height;
    emscripten_get_screen_size(&width, &height);

    return Vector2u(Vector2i(width, height));
}


////////////////////////////////////////////////////////////
void WindowImplEmscripten::setSize(const Vector2u& /*size*/)
{
    // Stub
}


////////////////////////////////////////////////////////////
void WindowImplEmscripten::setTitle(const String& title)
{
    emscripten_set_window_title(title.toAnsiString().c_str());
}


////////////////////////////////////////////////////////////
void WindowImplEmscripten::setIcon(unsigned int width, unsigned int height, const Uint8* pixels)
{
    // X11 wants BGRA pixels: swap red and blue channels
    // Note: this memory will be freed by XDestroyImage
    auto* iconPixels = static_cast<Uint8*>(std::malloc(width * height * 4));
    for (std::size_t i = 0; i < width * height; ++i)
    {
        iconPixels[i * 4 + 0] = pixels[i * 4 + 2];
        iconPixels[i * 4 + 1] = pixels[i * 4 + 1];
        iconPixels[i * 4 + 2] = pixels[i * 4 + 0];
        iconPixels[i * 4 + 3] = pixels[i * 4 + 3];
    }

    // Create the icon pixmap
    Visual*      defVisual = DefaultVisual(m_display, m_screen);
    auto defDepth  = static_cast<unsigned int>(DefaultDepth(m_display, m_screen));
    XImage* iconImage = XCreateImage(m_display, defVisual, defDepth, ZPixmap, 0, reinterpret_cast<char*>(iconPixels), width, height, 32, 0);
    if (!iconImage)
    {
        err() << "Failed to set the window's icon" << std::endl;
        return;
    }

    if (m_iconPixmap)
        XFreePixmap(m_display, m_iconPixmap);

    if (m_iconMaskPixmap)
        XFreePixmap(m_display, m_iconMaskPixmap);

    m_iconPixmap = XCreatePixmap(m_display, RootWindow(m_display, m_screen), width, height, defDepth);
    XGCValues values;
    GC iconGC = XCreateGC(m_display, m_iconPixmap, 0, &values);
    XPutImage(m_display, m_iconPixmap, iconGC, iconImage, 0, 0, 0, 0, width, height);
    XFreeGC(m_display, iconGC);
    XDestroyImage(iconImage);

    // Create the mask pixmap (must have 1 bit depth)
    std::size_t pitch = (width + 7) / 8;
    std::vector<Uint8> maskPixels(pitch * height, 0);
    for (std::size_t j = 0; j < height; ++j)
    {
        for (std::size_t i = 0; i < pitch; ++i)
        {
            for (std::size_t k = 0; k < 8; ++k)
            {
                if (i * 8 + k < width)
                {
                    Uint8 opacity = (pixels[(i * 8 + k + j * width) * 4 + 3] > 0) ? 1 : 0;
                    maskPixels[i + j * pitch] |= static_cast<Uint8>(opacity << k);
                }
            }
        }
    }
    m_iconMaskPixmap = XCreatePixmapFromBitmapData(m_display, m_window, reinterpret_cast<char*>(maskPixels.data()), width, height, 1, 0, 1);

    // Send our new icon to the window through the WMHints
    XWMHints* hints = XAllocWMHints();
    hints->flags       = IconPixmapHint | IconMaskHint;
    hints->icon_pixmap = m_iconPixmap;
    hints->icon_mask   = m_iconMaskPixmap;
    XSetWMHints(m_display, m_window, hints);
    XFree(hints);

    // ICCCM wants BGRA pixels: swap red and blue channels
    // ICCCM also wants the first 2 unsigned 32-bit values to be width and height
    std::vector<unsigned long> icccmIconPixels(2 + width * height, 0);
    unsigned long* ptr = icccmIconPixels.data();

    #pragma GCC diagnostic push
    #pragma GCC diagnostic ignored "-Wnull-dereference" // False positive.
    *ptr++ = width;
    *ptr++ = height;
    #pragma GCC diagnostic pop

    for (std::size_t i = 0; i < width * height; ++i)
    {
        *ptr++ = static_cast<unsigned long>((pixels[i * 4 + 2] << 0 ) |
                                            (pixels[i * 4 + 1] << 8 ) |
                                            (pixels[i * 4 + 0] << 16) |
                                            (pixels[i * 4 + 3] << 24));
    }

    Atom netWmIcon = getAtom("_NET_WM_ICON");

    XChangeProperty(m_display,
                    m_window,
                    netWmIcon,
                    XA_CARDINAL,
                    32,
                    PropModeReplace,
                    reinterpret_cast<const unsigned char*>(icccmIconPixels.data()),
                    static_cast<int>(2 + width * height));

    XFlush(m_display);
}


////////////////////////////////////////////////////////////
void WindowImplEmscripten::setVisible(bool /*visible*/)
{
    // Stub
}


////////////////////////////////////////////////////////////
void WindowImplEmscripten::setMouseCursorVisible(bool /*visible*/)
{
    // Stub
}


////////////////////////////////////////////////////////////
void WindowImplEmscripten::setMouseCursor(const CursorImpl& /* cursor */)
{
    // Stub
}


////////////////////////////////////////////////////////////
void WindowImplEmscripten::setMouseCursorGrabbed(bool /*grabbed*/)
{
    // Stub
}


////////////////////////////////////////////////////////////
void WindowImplEmscripten::setKeyRepeatEnabled(bool enabled)
{
    m_keyRepeat = enabled;
}


////////////////////////////////////////////////////////////
void WindowImplEmscripten::requestFocus()
{
    using namespace WindowsImplEmscriptenImpl;

    // Focus is only stolen among SFML windows, not between applications
    // Check the global list of windows to find out whether an SFML window has the focus
    // Note: can't handle console and other non-SFML windows belonging to the application.
    bool sfmlWindowFocused = false;

    {
        std::scoped_lock lock(allWindowsMutex);
        for (sf::priv::WindowImplEmscripten* windowPtr : allWindows)
        {
            if (windowPtr->hasFocus())
            {
                sfmlWindowFocused = true;
                break;
            }
        }
    }

    // Check if window is viewable (not on other desktop, ...)
    // TODO: Check also if minimized
    XWindowAttributes attributes;
    if (XGetWindowAttributes(m_display, m_window, &attributes) == 0)
    {
        sf::err() << "Failed to check if window is viewable while requesting focus" << std::endl;
        return; // error getting attribute
    }

    bool windowViewable = (attributes.map_state == IsViewable);

    if (sfmlWindowFocused && windowViewable)
    {
        // Another SFML window of this application has the focus and the current window is viewable:
        // steal focus (i.e. bring window to the front and give it input focus)
        grabFocus();
    }
    else
    {
        // Otherwise: display urgency hint (flashing application logo)
        // Ensure WM hints exist, allocate if necessary
        XWMHints* hints = XGetWMHints(m_display, m_window);
        if (hints == nullptr)
            hints = XAllocWMHints();

        // Add urgency (notification) flag to hints
        hints->flags |= XUrgencyHint;
        XSetWMHints(m_display, m_window, hints);
        XFree(hints);
    }
}


////////////////////////////////////////////////////////////
bool WindowImplEmscripten::hasFocus() const
{
    ::Window focusedWindow = 0;
    int revertToReturn = 0;
    XGetInputFocus(m_display, &focusedWindow, &revertToReturn);

    return (m_window == focusedWindow);
}


////////////////////////////////////////////////////////////
void WindowImplEmscripten::grabFocus()
{
    // Stub
}


////////////////////////////////////////////////////////////
void WindowImplEmscripten::setVideoMode(const VideoMode& mode)
{
    using namespace WindowsImplEmscriptenImpl;

    // Skip mode switching if the new mode is equal to the desktop mode
    if (mode == VideoMode::getDesktopMode())
        return;

    // Check if the XRandR extension is present
    int xRandRMajor, xRandRMinor;
    if (!checkXRandR(xRandRMajor, xRandRMinor))
    {
        // XRandR extension is not supported: we cannot use fullscreen mode
        err() << "Fullscreen is not supported, switching to window mode" << std::endl;
        return;
    }

    // Get root window
    //::Window rootWindow = RootWindow(m_display, m_screen);

    /*
    // Get the screen resources
    XRRScreenResources* res = XRRGetScreenResources(m_display, rootWindow);
    if (!res)
    {
        err() << "Failed to get the current screen resources for fullscreen mode, switching to window mode" << std::endl;
        return;
    }

    RROutput output = getOutputPrimary(rootWindow, res, xRandRMajor, xRandRMinor);

    // Get output info from output
    XRROutputInfo* outputInfo = XRRGetOutputInfo(m_display, res, output);
    if (!outputInfo || outputInfo->connection == RR_Disconnected)
    {
        XRRFreeScreenResources(res);

        // If outputInfo->connection == RR_Disconnected, free output info
        if (outputInfo)
            XRRFreeOutputInfo(outputInfo);

        err() << "Failed to get output info for fullscreen mode, switching to window mode" << std::endl;
        return;
    }

    // Retreive current RRMode, screen position and rotation
    XRRCrtcInfo* crtcInfo = XRRGetCrtcInfo(m_display, res, outputInfo->crtc);
    if (!crtcInfo)
    {
        XRRFreeScreenResources(res);
        XRRFreeOutputInfo(outputInfo);
        err() << "Failed to get crtc info for fullscreen mode, switching to window mode" << std::endl;
        return;
    }

    // Find RRMode to set
    bool modeFound = false;
    RRMode xRandMode;

    for (int i = 0; (i < res->nmode) && !modeFound; i++)
    {
        if (crtcInfo->rotation == RR_Rotate_90 || crtcInfo->rotation == RR_Rotate_270)
            std::swap(res->modes[i].height, res->modes[i].width);

        // Check if screen size match
        if ((res->modes[i].width == mode.width) &&
            (res->modes[i].height == mode.height))
        {
            xRandMode = res->modes[i].id;
            modeFound = true;
        }
    }

    if (!modeFound)
    {
        XRRFreeScreenResources(res);
        XRRFreeOutputInfo(outputInfo);
        err() << "Failed to find a matching RRMode for fullscreen mode, switching to window mode" << std::endl;
        return;
    }


    // Save the current video mode before we switch to fullscreen
    //m_oldVideoMode = crtcInfo->mode;
    //m_oldRRCrtc = outputInfo->crtc;

    // Switch to fullscreen mode
    XRRSetCrtcConfig(m_display,
                     res,
                     outputInfo->crtc,
                     CurrentTime,
                     crtcInfo->x,
                     crtcInfo->y,
                     xRandMode,
                     crtcInfo->rotation,
                     &output,
                     1);

    // Set "this" as the current fullscreen window
    fullscreenWindow = this;

    XRRFreeScreenResources(res);
    XRRFreeOutputInfo(outputInfo);
    XRRFreeCrtcInfo(crtcInfo);
    */
}


////////////////////////////////////////////////////////////
void WindowImplEmscripten::resetVideoMode()
{
    using namespace WindowsImplEmscriptenImpl;

    if (fullscreenWindow == this)
    {
        /*
        // Try to set old configuration
        // Check if the XRandR extension
        int xRandRMajor, xRandRMinor;
        if (checkXRandR(xRandRMajor, xRandRMinor))
        {
            XRRScreenResources* res = XRRGetScreenResources(m_display, DefaultRootWindow(m_display));
            if (!res)
            {
                err() << "Failed to get the current screen resources to reset the video mode" << std::endl;
                return;
            }

            // Retreive current screen position and rotation
            XRRCrtcInfo* crtcInfo = XRRGetCrtcInfo(m_display, res, m_oldRRCrtc);
            if (!crtcInfo)
            {
                XRRFreeScreenResources(res);
                err() << "Failed to get crtc info to reset the video mode" << std::endl;
                return;
            }

            RROutput output;

            // if version >= 1.3 get the primary screen else take the first screen
            if ((xRandRMajor == 1 && xRandRMinor >= 3) || xRandRMajor > 1)
            {
                output = XRRGetOutputPrimary(m_display, DefaultRootWindow(m_display));

                // Check if returned output is valid, otherwise use the first screen
                if (output == None)
                    output = res->outputs[0];
            }
            else{
                output = res->outputs[0];
            }

            XRRSetCrtcConfig(m_display,
                             res,
                             m_oldRRCrtc,
                             CurrentTime,
                             crtcInfo->x,
                             crtcInfo->y,
                             m_oldVideoMode,
                             crtcInfo->rotation,
                             &output,
                             1);

            XRRFreeCrtcInfo(crtcInfo);
            XRRFreeScreenResources(res);
        }
        */

        // Reset the fullscreen window
        fullscreenWindow = nullptr;
    }
}


////////////////////////////////////////////////////////////
void WindowImplEmscripten::switchToFullscreen()
{
    using namespace WindowsImplEmscriptenImpl;

    grabFocus();

    if (false)
    {
        Atom netWmBypassCompositor = getAtom("_NET_WM_BYPASS_COMPOSITOR");

        if (netWmBypassCompositor)
        {
            static const unsigned long bypassCompositor = 1;

            XChangeProperty(m_display,
                            m_window,
                            netWmBypassCompositor,
                            XA_CARDINAL,
                            32,
                            PropModeReplace,
                            reinterpret_cast<const unsigned char*>(&bypassCompositor),
                            1);
        }

        Atom netWmState = getAtom("_NET_WM_STATE", true);
        Atom netWmStateFullscreen = getAtom("_NET_WM_STATE_FULLSCREEN", true);

        if (!netWmState || !netWmStateFullscreen)
        {
            err() << "Setting fullscreen failed. Could not get required atoms" << std::endl;
            return;
        }

        XEvent event;
        std::memset(&event, 0, sizeof(event));

        event.type = ClientMessage;
        event.xclient.window = m_window;
        event.xclient.format = 32;
        event.xclient.message_type = netWmState;
        event.xclient.data.l[0] = 1; // _NET_WM_STATE_ADD
        event.xclient.data.l[1] = static_cast<long>(netWmStateFullscreen);
        event.xclient.data.l[2] = 0; // No second property
        event.xclient.data.l[3] = 1; // Normal window

        int result = XSendEvent(m_display,
                                DefaultRootWindow(m_display),
                                False,
                                SubstructureNotifyMask | SubstructureRedirectMask,
                                &event);

        if (!result)
            err() << "Setting fullscreen failed, could not send \"_NET_WM_STATE\" event" << std::endl;
    }
}


////////////////////////////////////////////////////////////
void WindowImplEmscripten::initialize()
{
    using namespace WindowsImplEmscriptenImpl;

    if (!m_inputContext)
        err() << "Failed to create input context for window -- TextEntered event won't be able to return unicode" << std::endl;

    // Add this window to the global list of windows (required for focus request)
    std::scoped_lock lock(allWindowsMutex);
    allWindows.push_back(this);
}


////////////////////////////////////////////////////////////
void WindowImplEmscripten::updateLastInputTime(::Time time)
{
    if (time && (time != m_lastInputTime))
    {
        Atom netWmUserTime = getAtom("_NET_WM_USER_TIME", true);

        if (netWmUserTime)
        {
            XChangeProperty(m_display,
                            m_window,
                            netWmUserTime,
                            XA_CARDINAL,
                            32,
                            PropModeReplace,
                            reinterpret_cast<const unsigned char*>(&time),
                            1);
        }

        m_lastInputTime = time;
    }
}


////////////////////////////////////////////////////////////
void WindowImplEmscripten::cleanup()
{
    // Restore the previous video mode (in case we were running in fullscreen)
    resetVideoMode();

    // Unhide the mouse cursor (in case it was hidden)
    setMouseCursorVisible(true);
}


////////////////////////////////////////////////////////////
bool WindowImplEmscripten::processEvent(XEvent& windowEvent)
{
    using namespace WindowsImplEmscriptenImpl;

    // This function implements a workaround to properly discard
    // repeated key events when necessary. The problem is that the
    // system's key events policy doesn't match SFML's one: X server will generate
    // both repeated KeyPress and KeyRelease events when maintaining a key down, while
    // SFML only wants repeated KeyPress events. Thus, we have to:
    // - Discard duplicated KeyRelease events when KeyRepeatEnabled is true
    // - Discard both duplicated KeyPress and KeyRelease events when KeyRepeatEnabled is false

    // Detect repeated key events
    if (windowEvent.type == KeyRelease)
    {
        // Find the next KeyPress event with matching keycode and time
        auto it = std::find_if(
            m_events.begin(),
            m_events.end(),
            KeyRepeatFinder(windowEvent.xkey.keycode, windowEvent.xkey.time)
        );

        if (it != m_events.end())
        {
            // If we don't want repeated events, remove the next KeyPress from the queue
            if (!m_keyRepeat)
                m_events.erase(it);

            // This KeyRelease is a repeated event and we don't want it
            return false;
        }
    }

    // Convert the X11 event to a sf::Event
    switch (windowEvent.type)
    {
        // Destroy event
        case DestroyNotify:
        {
            // The window is about to be destroyed: we must cleanup resources
            cleanup();
            break;
        }

        // Gain focus event
        case FocusIn:
        {
            // Update the input context
            if (m_inputContext)
                XSetICFocus(m_inputContext);

            // Grab cursor
            if (m_cursorGrabbed)
            {
                // Try multiple times to grab the cursor
                for (unsigned int trial = 0; trial < maxTrialsCount; ++trial)
                {
                    int result = XGrabPointer(m_display, m_window, True, None, GrabModeAsync, GrabModeAsync, m_window, None, CurrentTime);

                    if (result == GrabSuccess)
                    {
                        m_cursorGrabbed = true;
                        break;
                    }

                    // The cursor grab failed, trying again after a small sleep
                    sf::sleep(sf::milliseconds(50));
                }

                if (!m_cursorGrabbed)
                    err() << "Failed to grab mouse cursor" << std::endl;
            }

            Event event;
            event.type = Event::GainedFocus;
            pushEvent(event);

            // If the window has been previously marked urgent (notification) as a result of a focus request, undo that
            XWMHints* hints = XGetWMHints(m_display, m_window);
            if (hints != nullptr)
            {
                // Remove urgency (notification) flag from hints
                hints->flags &= ~XUrgencyHint;
                XSetWMHints(m_display, m_window, hints);
                XFree(hints);
            }

            break;
        }

        // Lost focus event
        case FocusOut:
        {
            // Update the input context
            if (m_inputContext)
                XUnsetICFocus(m_inputContext);

            // Release cursor
            if (m_cursorGrabbed)
                XUngrabPointer(m_display, CurrentTime);

            Event event;
            event.type = Event::LostFocus;
            pushEvent(event);
            break;
        }

        // Resize event
        case ConfigureNotify:
        {
            // ConfigureNotify can be triggered for other reasons, check if the size has actually changed
            if ((windowEvent.xconfigure.width != m_previousSize.x) || (windowEvent.xconfigure.height != m_previousSize.y))
            {
                Event event;
                event.type        = Event::Resized;
                event.size.width  = static_cast<unsigned int>(windowEvent.xconfigure.width);
                event.size.height = static_cast<unsigned int>(windowEvent.xconfigure.height);
                pushEvent(event);

                m_previousSize.x = windowEvent.xconfigure.width;
                m_previousSize.y = windowEvent.xconfigure.height;
            }
            break;
        }

        // Close event
        case ClientMessage:
        {
            // Input methods might want random ClientMessage events
            if (!XFilterEvent(&windowEvent, None))
            {
                static Atom wmProtocols = getAtom("WM_PROTOCOLS");

                // Handle window manager protocol messages we support
                if (windowEvent.xclient.message_type == wmProtocols)
                {
                    static Atom wmDeleteWindow = getAtom("WM_DELETE_WINDOW");
                    static Atom netWmPing = false ? getAtom("_NET_WM_PING", true) : None;

                    if ((windowEvent.xclient.format == 32) && (windowEvent.xclient.data.l[0]) == static_cast<long>(wmDeleteWindow))
                    {
                        // Handle the WM_DELETE_WINDOW message
                        Event event;
                        event.type = Event::Closed;
                        pushEvent(event);
                    }
                    else if (netWmPing && (windowEvent.xclient.format == 32) && (windowEvent.xclient.data.l[0]) == static_cast<long>(netWmPing))
                    {
                        // Handle the _NET_WM_PING message, send pong back to WM to show that we are responsive
                        windowEvent.xclient.window = DefaultRootWindow(m_display);

                        XSendEvent(m_display, DefaultRootWindow(m_display), False, SubstructureNotifyMask | SubstructureRedirectMask, &windowEvent);
                    }
                }
            }
            break;
        }

        // Key down event
        case KeyPress:
        {
            Keyboard::Key key = Keyboard::Unknown;

            // Try each KeySym index (modifier group) until we get a match
            for (int i = 0; i < 4; ++i)
            {
                // Get the SFML keyboard code from the keysym of the key that has been pressed
                key = keysymToSF(XLookupKeysym(&windowEvent.xkey, i));

                if (key != Keyboard::Unknown)
                    break;
            }

            // Fill the event parameters
            // TODO: if modifiers are wrong, use XGetModifierMapping to retrieve the actual modifiers mapping
            Event event;
            event.type        = Event::KeyPressed;
            event.key.code    = key;
            event.key.alt     = windowEvent.xkey.state & Mod1Mask;
            event.key.control = windowEvent.xkey.state & ControlMask;
            event.key.shift   = windowEvent.xkey.state & ShiftMask;
            event.key.system  = windowEvent.xkey.state & Mod4Mask;
            pushEvent(event);

            // Generate a TextEntered event
            if (!XFilterEvent(&windowEvent, None))
            {
                #ifdef X_HAVE_UTF8_STRING
                if (m_inputContext)
                {
                    Status status;
                    Uint8  keyBuffer[64];

                    int length = Xutf8LookupString(
                        m_inputContext,
                        &windowEvent.xkey,
                        reinterpret_cast<char*>(keyBuffer),
                        sizeof(keyBuffer),
                        nullptr,
                        &status
                    );

                    if (status == XBufferOverflow)
                        err() << "A TextEntered event has more than 64 bytes of UTF-8 input, and "
                                 "has been discarded\nThis means either you have typed a very long string "
                                 "(more than 20 chars), or your input method is broken in obscure ways." << std::endl;
                    else if (status == XLookupChars)
                    {
                        // There might be more than 1 characters in this event,
                        // so we must iterate it
                        Uint32 unicode = 0;
                        Uint8* iter = keyBuffer;
                        while (iter < keyBuffer + length)
                        {
                            iter = Utf8::decode(iter, keyBuffer + length, unicode, 0);
                            if (unicode != 0)
                            {
                                Event textEvent;
                                textEvent.type         = Event::TextEntered;
                                textEvent.text.unicode = unicode;
                                pushEvent(textEvent);
                            }
                        }
                    }
                }
                else
                #endif
                {
                    static XComposeStatus status;
                    char keyBuffer[16];
                    if (XLookupString(&windowEvent.xkey, keyBuffer, sizeof(keyBuffer), nullptr, &status))
                    {
                        Event textEvent;
                        textEvent.type         = Event::TextEntered;
                        textEvent.text.unicode = static_cast<Uint32>(keyBuffer[0]);
                        pushEvent(textEvent);
                    }
                }
            }

            updateLastInputTime(windowEvent.xkey.time);

            break;
        }

        // Key up event
        case KeyRelease:
        {
            Keyboard::Key key = Keyboard::Unknown;

            // Try each KeySym index (modifier group) until we get a match
            for (int i = 0; i < 4; ++i)
            {
                // Get the SFML keyboard code from the keysym of the key that has been released
                key = keysymToSF(XLookupKeysym(&windowEvent.xkey, i));

                if (key != Keyboard::Unknown)
                    break;
            }

            // Fill the event parameters
            Event event;
            event.type        = Event::KeyReleased;
            event.key.code    = key;
            event.key.alt     = windowEvent.xkey.state & Mod1Mask;
            event.key.control = windowEvent.xkey.state & ControlMask;
            event.key.shift   = windowEvent.xkey.state & ShiftMask;
            event.key.system  = windowEvent.xkey.state & Mod4Mask;
            pushEvent(event);

            break;
        }

        // Mouse button pressed
        case ButtonPress:
        {
            // XXX: Why button 8 and 9?
            // Because 4 and 5 are the vertical wheel and 6 and 7 are horizontal wheel ;)
            unsigned int button = windowEvent.xbutton.button;
            if ((button == Button1) ||
                (button == Button2) ||
                (button == Button3) ||
                (button == 8) ||
                (button == 9))
            {
                Event event;
                event.type          = Event::MouseButtonPressed;
                event.mouseButton.x = windowEvent.xbutton.x;
                event.mouseButton.y = windowEvent.xbutton.y;
                switch(button)
                {
                    case Button1: event.mouseButton.button = Mouse::Left;     break;
                    case Button2: event.mouseButton.button = Mouse::Middle;   break;
                    case Button3: event.mouseButton.button = Mouse::Right;    break;
                    case 8:       event.mouseButton.button = Mouse::XButton1; break;
                    case 9:       event.mouseButton.button = Mouse::XButton2; break;
                }
                pushEvent(event);
            }

            updateLastInputTime(windowEvent.xbutton.time);

            break;
        }

        // Mouse button released
        case ButtonRelease:
        {
            unsigned int button = windowEvent.xbutton.button;
            if ((button == Button1) ||
                (button == Button2) ||
                (button == Button3) ||
                (button == 8) ||
                (button == 9))
            {
                Event event;
                event.type          = Event::MouseButtonReleased;
                event.mouseButton.x = windowEvent.xbutton.x;
                event.mouseButton.y = windowEvent.xbutton.y;
                switch(button)
                {
                    case Button1: event.mouseButton.button = Mouse::Left;     break;
                    case Button2: event.mouseButton.button = Mouse::Middle;   break;
                    case Button3: event.mouseButton.button = Mouse::Right;    break;
                    case 8:       event.mouseButton.button = Mouse::XButton1; break;
                    case 9:       event.mouseButton.button = Mouse::XButton2; break;
                }
                pushEvent(event);
            }
            else if ((button == Button4) || (button == Button5))
            {
                Event event;

                event.type                   = Event::MouseWheelScrolled;
                event.mouseWheelScroll.wheel = Mouse::VerticalWheel;
                event.mouseWheelScroll.delta = (button == Button4) ? 1 : -1;
                event.mouseWheelScroll.x     = windowEvent.xbutton.x;
                event.mouseWheelScroll.y     = windowEvent.xbutton.y;
                pushEvent(event);
            }
            else if ((button == 6) || (button == 7))
            {
                Event event;
                event.type                   = Event::MouseWheelScrolled;
                event.mouseWheelScroll.wheel = Mouse::HorizontalWheel;
                event.mouseWheelScroll.delta = (button == 6) ? 1 : -1;
                event.mouseWheelScroll.x     = windowEvent.xbutton.x;
                event.mouseWheelScroll.y     = windowEvent.xbutton.y;
                pushEvent(event);
            }
            break;
        }

        // Mouse moved
        case MotionNotify:
        {
            Event event;
            event.type        = Event::MouseMoved;
            event.mouseMove.x = windowEvent.xmotion.x;
            event.mouseMove.y = windowEvent.xmotion.y;
            pushEvent(event);
            break;
        }

        // Mouse entered
        case EnterNotify:
        {
            if (windowEvent.xcrossing.mode == NotifyNormal)
            {
                Event event;
                event.type = Event::MouseEntered;
                pushEvent(event);
            }
            break;
        }

        // Mouse left
        case LeaveNotify:
        {
            if (windowEvent.xcrossing.mode == NotifyNormal)
            {
                Event event;
                event.type = Event::MouseLeft;
                pushEvent(event);
            }
            break;
        }

        // Window unmapped
        case UnmapNotify:
        {
            if (windowEvent.xunmap.window == m_window)
                m_windowMapped = false;

            break;
        }

        // Window visibility change
        case VisibilityNotify:
        {
            // We prefer using VisibilityNotify over MapNotify because
            // some window managers like awesome don't internally flag a
            // window as viewable even after it is mapped but before it
            // is visible leading to certain function calls failing with
            // an unviewable error if called before VisibilityNotify arrives

            // Empirical testing on most widely used window managers shows
            // that mapping a window will always lead to a VisibilityNotify
            // event that is not VisibilityFullyObscured
            if (windowEvent.xvisibility.window == m_window)
            {
                if (windowEvent.xvisibility.state != VisibilityFullyObscured)
                    m_windowMapped = true;
            }

            break;
        }

        // Window property change
        case PropertyNotify:
        {
            if (!m_lastInputTime)
                m_lastInputTime = windowEvent.xproperty.time;

            break;
        }
    }

    return true;
}


////////////////////////////////////////////////////////////
bool WindowImplEmscripten::checkXRandR(int& xRandRMajor, int& xRandRMinor)
{
    // Check if the XRandR extension is present
    int version;
    if (!XQueryExtension(m_display, "RANDR", &version, &version, &version))
    {
        err() << "XRandR extension is not supported" << std::endl;
        return false;
    }

    // dummy
    if (xRandRMajor == xRandRMinor)
        ;

    /*
    // Check XRandR version, 1.2 required
    if (!XRRQueryVersion(m_display, &xRandRMajor, &xRandRMinor) || xRandRMajor < 1 || (xRandRMajor == 1 && xRandRMinor < 2 ))
    {
        err() << "XRandR is too old" << std::endl;
        return false;
    }
    */
    return false;

    return true;
}


/*
////////////////////////////////////////////////////////////
RROutput WindowImplEmscripten::getOutputPrimary(::Window& rootWindow, XRRScreenResources* res, int xRandRMajor, int xRandRMinor)
{
    // if xRandR version >= 1.3 get the primary screen else take the first screen
    if ((xRandRMajor == 1 && xRandRMinor >= 3) || xRandRMajor > 1)
    {
        RROutput output = XRRGetOutputPrimary(m_display, rootWindow);

        // Check if returned output is valid, otherwise use the first screen
        if (output == None)
            return res->outputs[0];
        else
            return output;
    }

    // xRandr version can't get the primary screen, use the first screen
    return res->outputs[0];
}
*/


////////////////////////////////////////////////////////////
Vector2i WindowImplEmscripten::getPrimaryMonitorPosition()
{
    Vector2i monitorPosition;

    // Get root window
    //::Window rootWindow = RootWindow(m_display, m_screen);

    // Get the screen resources
    /*
    XRRScreenResources* res = XRRGetScreenResources(m_display, rootWindow);
    if (!res)
    {
        err() << "Failed to get the current screen resources for.primary monitor position" << std::endl;
        return monitorPosition;
    }

    // Get xRandr version
    int xRandRMajor, xRandRMinor;
    if (!checkXRandR(xRandRMajor, xRandRMinor))
        xRandRMajor = xRandRMinor = 0;

    RROutput output = getOutputPrimary(rootWindow, res, xRandRMajor, xRandRMinor);

    // Get output info from output
    XRROutputInfo* outputInfo = XRRGetOutputInfo(m_display, res, output);
    if (!outputInfo || outputInfo->connection == RR_Disconnected)
    {
        XRRFreeScreenResources(res);

        // If outputInfo->connection == RR_Disconnected, free output info
        if (outputInfo)
            XRRFreeOutputInfo(outputInfo);

        err() << "Failed to get output info for.primary monitor position" << std::endl;
        return monitorPosition;
    }

    // Retreive current RRMode, screen position and rotation
    XRRCrtcInfo* crtcInfo = XRRGetCrtcInfo(m_display, res, outputInfo->crtc);
    if (!crtcInfo)
    {
        XRRFreeScreenResources(res);
        XRRFreeOutputInfo(outputInfo);
        err() << "Failed to get crtc info for.primary monitor position" << std::endl;
        return monitorPosition;
    }

    monitorPosition.x = crtcInfo->x;
    monitorPosition.y = crtcInfo->y;

    XRRFreeCrtcInfo(crtcInfo);
    XRRFreeOutputInfo(outputInfo);
    XRRFreeScreenResources(res);
    */

    return monitorPosition;
}

} // namespace priv

} // namespace sf
