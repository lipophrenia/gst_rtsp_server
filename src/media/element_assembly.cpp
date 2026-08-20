#include "media/element_assembly.hpp"

#include <algorithm>

ElementAssembly::ElementAssembly(const char *binName)
    : bin_(gst_bin_new(binName))
{
}

ElementAssembly::~ElementAssembly()
{
    for (GstElement *element : unparentedElements_) {
        gst_object_unref(element);
    }
}

bool ElementAssembly::valid() const
{
    return bin_.get() != NULL;
}

GstElement *ElementAssembly::make(const char *factoryName, const char *elementName)
{
    GstElement *element = gst_element_factory_make(factoryName, elementName);
    if (element) {
        unparentedElements_.push_back(element);
    }
    return element;
}

bool ElementAssembly::add(std::initializer_list<GstElement *> elements)
{
    if (!bin_) {
        return false;
    }
    for (GstElement *element : elements) {
        if (!element || !gst_bin_add(GST_BIN(bin_.get()), element)) {
            return false;
        }
        releaseElementOwnership(element);
    }
    return true;
}

GstElement *ElementAssembly::release()
{
    return bin_.release();
}

void ElementAssembly::releaseElementOwnership(GstElement *element)
{
    const std::vector<GstElement *>::iterator position =
        std::find(unparentedElements_.begin(), unparentedElements_.end(), element);
    if (position != unparentedElements_.end()) {
        unparentedElements_.erase(position);
    }
}
