// MatrixGame - licensed under GPLv2 or any later version.
#pragma once

namespace Graphics {

// A created interface owns its initial COM reference; a host pointer is borrowed.
template<class Interface>
class Reference {
    Interface *owned_ = nullptr;
public:
    Reference() = default;
    Reference(const Reference &) = delete;
    Reference &operator=(const Reference &) = delete;
    void adopt(Interface *pointer) { owned_ = pointer; }
    void release(Interface *&visible) {
        Interface *pointer = owned_;
        owned_ = nullptr;
        visible = nullptr;
        if (pointer) pointer->Release();
    }
};

} // namespace Graphics
