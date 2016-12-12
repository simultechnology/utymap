#include "builders/terrain/ExteriorGenerator.hpp"
#include "entities/Way.hpp"
#include "entities/Area.hpp"
#include "entities/Relation.hpp"
#include "math/Vector2.hpp"
#include "utils/ElementUtils.hpp"

#include <climits>
#include <unordered_set>
#include <unordered_map>

using namespace ClipperLib;
using namespace utymap;
using namespace utymap::builders;
using namespace utymap::mapcss;
using namespace utymap::math;

namespace {
const double Scale = 1E7;

const std::string TerrainMeshName = "terrain_exterior";

const std::string InclineKey = "inclide";
const std::string InclineUpValue = "up";
const std::string InclineDownValue = "down";

double interpolate(double a, double b, double r)
{
    return a * (1.0 - r) + b * r;
}

struct HashFunc
{
    size_t operator()(const Vector2& v) const
    {
        size_t h1 = std::hash<double>()(v.x);
        size_t h2 = std::hash<double>()(v.y);
        return h1 ^ h2;
    }
};

struct EqualsFunc
{
    bool operator()(const Vector2& lhs, const Vector2& rhs) const
    {
        return lhs == rhs;
    }
};

struct InclineType final
{
    const static InclineType None;
    const static InclineType Up;
    const static InclineType Down;

    // TODO support specific incline definitions.

private:
    InclineType(int value) : value_(value)
    {
    }

    int value_;
};

const InclineType InclineType::None = InclineType(0);
const InclineType InclineType::Up = InclineType(std::numeric_limits<int>::max());
const InclineType InclineType::Down = InclineType(std::numeric_limits<int>::min());

/// Stores information for building slope region.
struct SlopeSegment
{
    Vector2 tail;
    std::shared_ptr<Region> region;

    // NOTE for debug only.
    // TODO remove
    std::size_t elementId;
    
    SlopeSegment(const Vector2& tail, const std::shared_ptr<Region>& region, std::size_t elementId) :
        tail(tail), region(region), elementId(elementId)
    {
    }
};

/// Encapsulates slope region used to calculate slope ration for given point.
class SlopeRegion final
{
public:
    SlopeRegion(const Vector2& start, const Vector2& end, const std::shared_ptr<Region>& region, std::size_t elementId) :
        region_(region), start_(start), centerLine_(end - start), lengthSquare_(), elementId(elementId)
    {
        double distance = Vector2::distance(start, end);
        lengthSquare_ = distance * distance;
    }

    bool contains(const IntPoint& p) const
    {
        for (const auto& path : region_->geometry)
            if (ClipperLib::PointInPolygon(p, path) != 0)
                return true;

        return false;
    }

    /// Calculate scalar projection divided to length to get slope ratio in range [0, 1]
    double calculateSlope(const Vector2& v) const
    {
        return(v - start_).dot(centerLine_) / lengthSquare_;
    }
    // NOTE for debug only.
    // TODO remove
    std::size_t elementId;

private:
    std::shared_ptr<Region> region_;
    const Vector2 start_;
    const Vector2 centerLine_;
    double lengthSquare_;
};

}

class ExteriorGenerator::ExteriorGeneratorImpl : public utymap::entities::ElementVisitor
{
public:
    ExteriorGeneratorImpl(const BuilderContext& context) :
        context_(context), inclineType_(InclineType::None), region_(nullptr),
        slopeRegions(), entranceMap_(), exitMap_(),
        inclineKey_(context.stringTable.getId(InclineKey)),
        inclideUp_(context.stringTable.getId(InclineUpValue)),
        inclideDown_(context.stringTable.getId(InclineDownValue))
    {
    }

    void visitNode(const utymap::entities::Node&) override 
    { 
        // TODO
    }

    void visitWay(const utymap::entities::Way& way) override
    {
        const auto& c1 = way.coordinates[0];
        const auto& c2 = way.coordinates[way.coordinates.size() - 1];

        auto v1 = Vector2(c1.longitude, c1.latitude);
        auto v2 = Vector2(c2.longitude, c2.latitude);

        if (region_->level == 0) {
            entranceMap_.insert(v1);
            entranceMap_.insert(v2);
        } else {
            exitMap_[v1].push_back(SlopeSegment(v2, region_, way.id));
            exitMap_[v2].push_back(SlopeSegment(v1, region_, way.id));
        }
    }

    void visitArea(const utymap::entities::Area& area) override 
    {
        // TODO
    }

    void visitRelation(const utymap::entities::Relation& relation) override
    {
        for (const auto& element : relation.elements)
            element->accept(*this);
    }

    void setElementData(const utymap::entities::Element& element, const Style& style, const std::shared_ptr<Region>& region)
    {
        region_ = region;

        auto inclineValue = utymap::utils::getTagValue(inclineKey_, element.tags, 0,
            [&](const std::uint32_t value) { return value; });

        if (inclineValue == inclideUp_)
            inclineType_ = InclineType::Up;
        else if (inclineValue == inclideDown_)
            inclineType_ = InclineType::Down;
        else
            inclineType_ = InclineType::None;
    }

    void build()
    {
        // Build slope regions.
        for (const auto& pair : exitMap_) {
            if (entranceMap_.find(pair.first) == entranceMap_.end())
                continue;

            for (const auto& segment : pair.second) {
                Vector2 middle((segment.tail.x + pair.first.x) / 2, (segment.tail.y + pair.first.y)/ 2);
                slopeRegions.push_back(SlopeRegion(middle, pair.first, segment.region, segment.elementId));
            }
        }
    }

    /// TODO optimize data structure.
    std::vector<SlopeRegion> slopeRegions;

private:
    const BuilderContext& context_;

    /// Defines set for storing exterior surface entrance info.
    std::unordered_set<Vector2, HashFunc, EqualsFunc> entranceMap_;

    /// Defines map for storing exterior surface exit info.
    std::unordered_map<Vector2, std::vector<SlopeSegment>, HashFunc, EqualsFunc> exitMap_;

    /// Mapcss cached values
    std::uint32_t inclineKey_, inclideUp_, inclideDown_;

    InclineType inclineType_;

    std::shared_ptr<Region> region_;
};


ExteriorGenerator::ExteriorGenerator(const BuilderContext& context, const Style& style, const Path& tileRect) :
    TerraGenerator(context, style, tileRect, TerrainMeshName), p_impl(utymap::utils::make_unique<ExteriorGeneratorImpl>(context))
{
}

void ExteriorGenerator::onNewRegion(const std::string& type, const utymap::entities::Element& element, const Style& style, const std::shared_ptr<Region>& region)
{
    // TODO implement this.
    if (region->level > 0)
        return;

    p_impl->setElementData(element, style, region);
    element.accept(*p_impl);
}

void ExteriorGenerator::generateFrom(Layers& layers)
{
    p_impl->build();

    for (const auto& layerPair : layers) {
        for (const auto& region : layerPair.second) {
            if (region->level < 0) {
                TerraGenerator::addGeometry(region->geometry, *region->context, [](const Path& path) { });
            }
        }
    }

    context_.meshCallback(mesh_);
}

ExteriorGenerator::~ExteriorGenerator()
{
}

void ExteriorGenerator::addGeometry(utymap::math::Polygon& polygon, const RegionContext& regionContext)
{
    double deepHeight = 10;

    context_.meshBuilder.addPolygon(mesh_, polygon, regionContext.geometryOptions, regionContext.appearanceOptions,
        [&](const GeoCoordinate& coordinate) {
        IntPoint p(coordinate.longitude * Scale, coordinate.latitude* Scale);
        Vector2 v(coordinate.longitude, coordinate.latitude);
        for (const auto& region : p_impl->slopeRegions) {
            if (region.contains(p)) {
                double slope = region.calculateSlope(v);
                if (slope >=0 )
                    return interpolate(0, deepHeight, slope);
            }
        }
        return deepHeight;
    });
    context_.meshBuilder.writeTextureMappingInfo(mesh_, regionContext.appearanceOptions);
}