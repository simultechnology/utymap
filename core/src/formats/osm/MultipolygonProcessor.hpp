#ifndef FORMATS_OSM_MULTIPOLYGONPROCESSOR_HPP_DEFINED
#define FORMATS_OSM_MULTIPOLYGONPROCESSOR_HPP_DEFINED

#include "GeoCoordinate.hpp"
#include "entities/Element.hpp"
#include "formats/FormatTypes.hpp"
#include "formats/osm/OsmDataContext.hpp"
#include "index/StringTable.hpp"

#include <memory>
#include <unordered_map>

namespace utymap { namespace formats {

// Builds relation from multipolygon (see http://wiki.openstreetmap.org/wiki/Talk:Relation:multipolygon)
struct MultipolygonProcessor
{
private:

    // Helper class which provides the way to handle coordinate sequences.
    struct CoordinateSequence;
    typedef std::vector<std::shared_ptr<MultipolygonProcessor::CoordinateSequence>> CoordinateSequences;
    typedef std::vector<utymap::entities::Tag> ElementTags;

public:
    MultipolygonProcessor(std::uint64_t id,
                          utymap::formats::RelationMembers& members,
                          const utymap::formats::Tags& tags,
                          utymap::index::StringTable& stringTable,
                          OsmDataContext context);

    // Builds relation from multipolygon relation.
    void process();

private:

    ElementTags getTags(const CoordinateSequence& outer) const;

    void simpleCase(utymap::entities::Relation& relation,
                    const CoordinateSequences& sequences,
                    const std::vector<int>& outerIndecies,
                    const std::vector<int>& innerIndecies);

    void complexCase(utymap::entities::Relation& relation, CoordinateSequences& sequences);

    CoordinateSequences createRings(CoordinateSequences& sequences);

    void fillRelation(utymap::entities::Relation& relation, CoordinateSequences& rings);

    std::uint64_t id_;
    utymap::formats::RelationMembers& members_;
    const utymap::formats::Tags& tags_;
    utymap::index::StringTable& stringTable_;
    utymap::formats::OsmDataContext context_;
};
}}

#endif // FORMATS_OSM_MULTIPOLYGONPROCESSOR_HPP_DEFINED
