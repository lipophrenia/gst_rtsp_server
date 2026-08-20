#pragma once

#include "gst/gst_raii.hpp"

#include <initializer_list>
#include <vector>

class ElementAssembly final {
public:
    explicit ElementAssembly(const char *binName);
    ~ElementAssembly();

    ElementAssembly(const ElementAssembly &) = delete;
    ElementAssembly &operator=(const ElementAssembly &) = delete;

    bool valid() const;
    GstElement *make(const char *factoryName, const char *elementName);
    bool add(std::initializer_list<GstElement *> elements);
    GstElement *release();

private:
    void releaseElementOwnership(GstElement *element);

    GstObjectPtr<GstElement> bin_;
    std::vector<GstElement *> unparentedElements_;
};
