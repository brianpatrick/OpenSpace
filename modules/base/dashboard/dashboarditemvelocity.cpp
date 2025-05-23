/*****************************************************************************************
 *                                                                                       *
 * OpenSpace                                                                             *
 *                                                                                       *
 * Copyright (c) 2014-2025                                                               *
 *                                                                                       *
 * Permission is hereby granted, free of charge, to any person obtaining a copy of this  *
 * software and associated documentation files (the "Software"), to deal in the Software *
 * without restriction, including without limitation the rights to use, copy, modify,    *
 * merge, publish, distribute, sublicense, and/or sell copies of the Software, and to    *
 * permit persons to whom the Software is furnished to do so, subject to the following   *
 * conditions:                                                                           *
 *                                                                                       *
 * The above copyright notice and this permission notice shall be included in all copies *
 * or substantial portions of the Software.                                              *
 *                                                                                       *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR IMPLIED,   *
 * INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, FITNESS FOR A         *
 * PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT    *
 * HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF  *
 * CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE  *
 * OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.                                         *
 ****************************************************************************************/

#include <modules/base/dashboard/dashboarditemvelocity.h>

#include <openspace/camera/camera.h>
#include <openspace/documentation/documentation.h>
#include <openspace/documentation/verifier.h>
#include <openspace/engine/globals.h>
#include <openspace/engine/windowdelegate.h>
#include <openspace/rendering/renderengine.h>
#include <openspace/scene/scene.h>
#include <openspace/scene/scenegraphnode.h>
#include <openspace/util/distanceconversion.h>
#include <ghoul/font/font.h>
#include <ghoul/logging/logmanager.h>
#include <ghoul/misc/profiling.h>

namespace {
    constexpr openspace::properties::Property::PropertyInfo SimplificationInfo = {
        "Simplification",
        "Simplification",
        "If this value is enabled, the velocity is displayed in nuanced units, such as "
        "m/s, AU/s, light years / s etc. If this value is disabled, the unit can be "
        "explicitly requested.",
        openspace::properties::Property::Visibility::User
    };

    constexpr openspace::properties::Property::PropertyInfo RequestedUnitInfo = {
        "RequestedUnit",
        "Requested Unit",
        "If the simplification is disabled, this distance unit is used for the velocity "
        "display.",
        openspace::properties::Property::Visibility::User
    };

    // This `DashboardItem` shows the velocity of the camera, that is how fast the camera
    // has moved since the last frame in the amount of time it took to render that frame.
    // The `Simplification` and `RequestedUnit` can be used to control which unit is used
    // to display the speed. By default, `Simplification` is enabled, which means that the
    // most natural unit, that is, the one that gives the least number of printed digits,
    // is used.
    struct [[codegen::Dictionary(DashboardItemVelocity)]] Parameters {
        // [[codegen::verbatim(SimplificationInfo.description)]]
        std::optional<bool> simplification;

        // [[codegen::verbatim(RequestedUnitInfo.description)]]
        std::optional<std::string> requestedUnit
            [[codegen::inlist(openspace::distanceUnitList())]];
    };
#include "dashboarditemvelocity_codegen.cpp"
} // namespace

namespace openspace {

documentation::Documentation DashboardItemVelocity::Documentation() {
    return codegen::doc<Parameters>(
        "base_dashboarditem_velocity",
        DashboardTextItem::Documentation()
    );
}

DashboardItemVelocity::DashboardItemVelocity(const ghoul::Dictionary& dictionary)
    : DashboardTextItem(dictionary)
    , _doSimplification(SimplificationInfo, true)
    , _requestedUnit(RequestedUnitInfo)
{
    const Parameters p = codegen::bake<Parameters>(dictionary);
    _doSimplification.onChange([this]() {
        _requestedUnit.setVisibility(
            _doSimplification ?
            properties::Property::Visibility::Hidden :
            properties::Property::Visibility::User
        );
    });
    _doSimplification = p.simplification.value_or(_doSimplification);
    addProperty(_doSimplification);

    for (const DistanceUnit u : DistanceUnits) {
        _requestedUnit.addOption(
            static_cast<int>(u),
            std::string(nameForDistanceUnit(u))
        );
    }
    _requestedUnit = static_cast<int>(DistanceUnit::Meter);
    if (p.requestedUnit.has_value()) {
        const DistanceUnit unit = distanceUnitFromString(*p.requestedUnit);
        _requestedUnit = static_cast<int>(unit);
        _doSimplification = false;
    }
    addProperty(_requestedUnit);
}

void DashboardItemVelocity::update() {
    ZoneScoped;

    const glm::dvec3 currentPos = global::renderEngine->scene()->camera()->positionVec3();
    const glm::dvec3 dt = currentPos - _prevPosition;
    _prevPosition = currentPos;

    const double speedPerFrame = glm::length(dt);
    const double secondsPerFrame = global::windowDelegate->averageDeltaTime();
    const double speedPerSecond = speedPerFrame / secondsPerFrame;

    std::pair<double, std::string_view> dist;
    if (_doSimplification) {
        dist = simplifyDistance(speedPerSecond);
    }
    else {
        const DistanceUnit unit = static_cast<DistanceUnit>(_requestedUnit.value());
        const double convertedD = convertMeters(speedPerSecond, unit);
        dist = std::pair(convertedD, nameForDistanceUnit(unit, convertedD != 1.0));
    }

    _buffer = std::format("Camera velocity: {:.4f} {}/s", dist.first, dist.second);
}

} // namespace openspace
