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

#include <modules/base/dashboard/dashboarditemdistance.h>

#include <openspace/camera/camera.h>
#include <openspace/documentation/documentation.h>
#include <openspace/documentation/verifier.h>
#include <openspace/engine/globals.h>
#include <openspace/navigation/navigationhandler.h>
#include <openspace/navigation/orbitalnavigator.h>
#include <openspace/rendering/renderengine.h>
#include <openspace/scene/scene.h>
#include <openspace/scene/scenegraphnode.h>
#include <openspace/util/distanceconversion.h>
#include <ghoul/font/font.h>
#include <ghoul/font/fontmanager.h>
#include <ghoul/font/fontrenderer.h>
#include <ghoul/logging/logmanager.h>
#include <ghoul/misc/profiling.h>

namespace {
    enum Type {
        Node = 0,
        NodeSurface,
        Focus,
        Camera
    };

    constexpr openspace::properties::Property::PropertyInfo SourceTypeInfo = {
        "SourceType",
        "Source Type",
        "The type of position that is used as the source to calculate the distance.",
        openspace::properties::Property::Visibility::User
    };

    constexpr openspace::properties::Property::PropertyInfo SourceNodeIdentifierInfo = {
        "SourceNodeIdentifier",
        "Source Node Identifier",
        "If a scene graph node is selected as type, this value specifies the identifier "
        "of the node that is to be used as the source for computing the distance.",
        openspace::properties::Property::Visibility::User
    };

    constexpr openspace::properties::Property::PropertyInfo DestinationTypeInfo = {
        "DestinationType",
        "Destination Type",
        "The type of position that is used as the destination to calculate the distance.",
        openspace::properties::Property::Visibility::User
    };

    constexpr openspace::properties::Property::PropertyInfo
        DestinationNodeIdentifierInfo =
    {
        "DestinationNodeIdentifier",
        "Destination Node Identifier",
        "If a scene graph node is selected as type, this value specifies the identifier "
        "of the node that is to be used as the destination for computing the distance.",
        openspace::properties::Property::Visibility::User
    };

    constexpr openspace::properties::Property::PropertyInfo SimplificationInfo = {
        "Simplification",
        "Simplification",
        "If this value is enabled, the distance is displayed in nuanced units, such as "
        "km, AU, light years, parsecs, etc. If this value is disabled, the unit can be "
        "explicitly requested.",
        openspace::properties::Property::Visibility::AdvancedUser
    };

    constexpr openspace::properties::Property::PropertyInfo RequestedUnitInfo = {
        "RequestedUnit",
        "Requested Unit",
        "If the simplification is disabled, this distance unit is used as a destination "
        "to convert the meters into.",
        openspace::properties::Property::Visibility::AdvancedUser
    };

    constexpr openspace::properties::Property::PropertyInfo FormatStringInfo = {
        "FormatString",
        "Format String",
        "The format string that is used for formatting the distance string.  This format "
        "receives four parameters:  The name of the source, the name of the destination "
        "the value of the distance and the unit of the distance.",
        openspace::properties::Property::Visibility::AdvancedUser
    };

    // This `DashboardItem` displays the distance between two points. The points can be
    // defined either by the location of a scene graph node, the surface of a scene graph
    // node's bounding sphere, the location of the current focus node, or the position of
    // the camera. These definitions can be mixed and matched to calculate any combination
    // of positions.
    //
    // The resulting text can be formatted in the `FormatString` and the measurement unit
    // is chosed by changing the `Simplification` and `RequestedUnit` parameters.
    struct [[codegen::Dictionary(DashboardItemDistance)]] Parameters {
        enum class [[codegen::map(Type)]] TypeInfo {
            Node,
            NodeSurface [[codegen::key("Node Surface")]],
            Focus,
            Camera
        };

        // [[codegen::verbatim(SourceTypeInfo.description)]]
        TypeInfo sourceType;

        // [[codegen::verbatim(SourceNodeIdentifierInfo.description)]]
        std::optional<std::string> sourceNodeIdentifier;

        // [[codegen::verbatim(DestinationTypeInfo.description)]]
        TypeInfo destinationType;

        // [[codegen::verbatim(DestinationNodeIdentifierInfo.description)]]
        std::optional<std::string> destinationNodeIdentifier;

        // [[codegen::verbatim(SimplificationInfo.description)]]
        std::optional<bool> simplification;

        // [[codegen::verbatim(RequestedUnitInfo.description)]]
        std::optional<std::string> requestedUnit
            [[codegen::inlist(openspace::distanceUnitList())]];

        // [[codegen::verbatim(FormatStringInfo.description)]]
        std::optional<std::string> formatString;
    };
#include "dashboarditemdistance_codegen.cpp"
} // namespace

namespace openspace {

documentation::Documentation DashboardItemDistance::Documentation() {
    return codegen::doc<Parameters>(
        "base_dashboarditem_distance",
        DashboardTextItem::Documentation()
    );
}

DashboardItemDistance::DashboardItemDistance(const ghoul::Dictionary& dictionary)
    : DashboardTextItem(dictionary)
    , _doSimplification(SimplificationInfo, true)
    , _requestedUnit(RequestedUnitInfo)
    , _formatString(FormatStringInfo, "Distance from {} to {}: {:f} {}")
    , _source{
        properties::OptionProperty(SourceTypeInfo),
        properties::StringProperty(SourceNodeIdentifierInfo),
        nullptr
    }
    , _destination{
        properties::OptionProperty(DestinationTypeInfo),
        properties::StringProperty(DestinationNodeIdentifierInfo),
        nullptr
    }
{
    const Parameters p = codegen::bake<Parameters>(dictionary);

    _source.type.addOptions({
        { Type::Node, "Node" },
        { Type::NodeSurface, "Node Surface" },
        { Type::Focus, "Focus" },
        { Type::Camera, "Camera" }
    });
    _source.type.onChange([this]() {
        _source.nodeIdentifier.setVisibility(
            properties::Property::Visibility(
                _source.type == Type::Node || _source.type == Type::NodeSurface
            )
        );
    });
    _source.type = codegen::map<Type>(p.sourceType);
    addProperty(_source.type);

    _source.nodeIdentifier.onChange([this]() { _source.node = nullptr; });
    if (_source.type == Type::Node || _source.type == Type::NodeSurface) {
        if (p.sourceNodeIdentifier.has_value()) {
            _source.nodeIdentifier = *p.sourceNodeIdentifier;
        }
        else {
            LERRORC(
                "DashboardItemDistance",
                "Node type was selected for source but no node specified"
            );
        }
    }
    addProperty(_source.nodeIdentifier);

    _destination.type.addOptions({
        { Type::Node, "Node" },
        { Type::NodeSurface, "Node Surface" },
        { Type::Focus, "Focus" },
        { Type::Camera, "Camera" }
    });
    _destination.type.onChange([this]() {
        _destination.nodeIdentifier.setVisibility(
            properties::Property::Visibility(
                _source.type == Type::Node || _source.type == Type::NodeSurface
            )
        );
    });
    _destination.type = codegen::map<Type>(p.destinationType);
    addProperty(_destination.type);
    _destination.nodeIdentifier.onChange([this]() { _destination.node = nullptr; });
    if (_destination.type == Type::Node || _destination.type == Type::NodeSurface) {
        if (p.destinationNodeIdentifier.has_value()) {
            _destination.nodeIdentifier = *p.destinationNodeIdentifier;
        }
        else {
            LERRORC(
                "DashboardItemDistance",
                "Node type was selected for destination but no node specified"
            );
        }
    }
    addProperty(_destination.nodeIdentifier);

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
    }
    addProperty(_requestedUnit);

    _formatString = p.formatString.value_or(_formatString);
    addProperty(_formatString);

    _localBuffer.resize(256);
}

std::pair<glm::dvec3, std::string> DashboardItemDistance::positionAndLabel(
                                                                      Component& mainComp,
                                                               Component& otherComp) const
{
    if ((mainComp.type == Type::Node) || (mainComp.type == Type::NodeSurface)) {
        if (!mainComp.node) [[unlikely]] {
            mainComp.node = global::renderEngine->scene()->sceneGraphNode(
                mainComp.nodeIdentifier
            );

            if (!mainComp.node) {
                LERRORC(
                    "DashboardItemDistance",
                    "Could not find node '" + mainComp.nodeIdentifier.value() + "'"
                );
                return { glm::dvec3(0.0), "Node" };
            }
        }
    }

    switch (mainComp.type) {
        case Type::Node:
            return { mainComp.node->worldPosition(), mainComp.node->guiName() };
        case Type::NodeSurface:
        {
            glm::dvec3 otherPos;
            if (otherComp.type == Type::NodeSurface) {
                // We are only interested in the direction, and we want to prevent
                // infinite recursion
                otherPos = otherComp.node->worldPosition();
            }
            else {
                otherPos = positionAndLabel(otherComp, mainComp).first;
            }
            const glm::dvec3 thisPos = mainComp.node->worldPosition();

            const glm::dvec3 dir = glm::normalize(otherPos - thisPos);
            const glm::dvec3 dirLen = dir * glm::dvec3(mainComp.node->boundingSphere());

            return { thisPos + dirLen, "surface of " + mainComp.node->guiName() };
        }
        case Type::Focus: {
            const SceneGraphNode* anchor =
                global::navigationHandler->orbitalNavigator().anchorNode();
            if (!anchor) {
                return { glm::dvec3(0.0), "Unknown" };
            }
            else {
                return { anchor->worldPosition(), "focus" };
            }
        }
        case Type::Camera:
            return { global::renderEngine->scene()->camera()->positionVec3(), "camera" };
        default:
            return { glm::dvec3(0.0), "Unknown" };
    }
}

void DashboardItemDistance::update() {
    ZoneScoped;

    std::pair<glm::dvec3, std::string> sourceInfo = positionAndLabel(
        _source,
        _destination
    );
    std::pair<glm::dvec3, std::string> destinationInfo = positionAndLabel(
        _destination,
        _source
    );

    const double d = glm::length(sourceInfo.first - destinationInfo.first);
    std::pair<double, std::string_view> dist;
    if (_doSimplification) {
        dist = simplifyDistance(d);
    }
    else {
        const DistanceUnit unit = static_cast<DistanceUnit>(_requestedUnit.value());
        const double convertedD = convertMeters(d, unit);
        dist = std::pair(convertedD, nameForDistanceUnit(unit, convertedD != 1.0));
    }

    std::fill(_localBuffer.begin(), _localBuffer.end(), char(0));
    try {
        // @CPP26(abock): This can be replaced with std::runtime_format
        char* end = std::vformat_to(
            _localBuffer.data(),
            _formatString.value(),
            std::make_format_args(
                sourceInfo.second,
                destinationInfo.second,
                dist.first,
                dist.second
            )
        );

        _buffer = std::string(_localBuffer.data(), end - _localBuffer.data());
    }
    catch (const std::format_error&) {
        LERRORC("DashboardItemDate", "Illegal format string");
    }
}

} // namespace openspace
