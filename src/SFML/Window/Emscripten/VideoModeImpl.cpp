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
#include <SFML/Window/VideoModeImpl.hpp>
#include <SFML/Window/Unix/Display.hpp>
#include <SFML/System/Err.hpp>
#include <algorithm>

#include <emscripten.h>

namespace sf
{
namespace priv
{
////////////////////////////////////////////////////////////
std::vector<VideoMode> VideoModeImpl::getFullscreenModes()
{
    Int32 width, height;
    emscripten_get_screen_size(&width, &height);

    // WebGL spec says depth is at least 16
    return { VideoMode(static_cast<Uint32>(width), static_cast<Uint32>(height), 16) };
}


////////////////////////////////////////////////////////////
VideoMode VideoModeImpl::getDesktopMode()
{
    Int32 width, height;
    emscripten_get_screen_size(&width, &height);

    // WebGL spec says depth is at least 16
    return VideoMode(static_cast<Uint32>(width), static_cast<Uint32>(height), 16);
}

} // namespace priv

} // namespace sf
