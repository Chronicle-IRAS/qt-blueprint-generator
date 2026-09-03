#include "blueprint/blueprint_document.h"

bool PortSpec::operator==(const PortSpec &other) const
{
    return name == other.name && type == other.type && description == other.description;
}

bool PortSpec::operator!=(const PortSpec &other) const
{
    return !(*this == other);
}

bool BlueprintNode::operator==(const BlueprintNode &other) const
{
    return id == other.id && type == other.type && name == other.name
        && description == other.description && inputs == other.inputs && outputs == other.outputs
        && constraints == other.constraints && acceptanceCriteria == other.acceptanceCriteria;
}

bool BlueprintNode::operator!=(const BlueprintNode &other) const
{
    return !(*this == other);
}

bool BlueprintEdge::operator==(const BlueprintEdge &other) const
{
    return id == other.id && source == other.source && target == other.target && label == other.label;
}

bool BlueprintEdge::operator!=(const BlueprintEdge &other) const
{
    return !(*this == other);
}

bool BlueprintDocument::operator==(const BlueprintDocument &other) const
{
    return schemaVersion == other.schemaVersion && projectId == other.projectId
        && projectName == other.projectName && target == other.target && nodes == other.nodes
        && edges == other.edges;
}

bool BlueprintDocument::operator!=(const BlueprintDocument &other) const
{
    return !(*this == other);
}
