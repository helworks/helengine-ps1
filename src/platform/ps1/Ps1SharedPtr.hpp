#pragma once

#include <cstddef>
#include <type_traits>

namespace helengine::ps1::detail {

/// <summary>Stores the type-erased lifetime operations for one PS1 shared owner set.</summary>
struct Ps1SharedControlBlock {
    std::size_t References;
    void (*DisposeObject)(Ps1SharedControlBlock*);
    void (*DestroyBlock)(Ps1SharedControlBlock*);
};

/// <summary>Owns one pointer and its custom deleter behind a PS1 control block.</summary>
template <typename TValue, typename TDeleter>
struct Ps1TypedSharedControlBlock final : Ps1SharedControlBlock {
    TValue* Value;
    TDeleter Deleter;

    /// <summary>Creates a single-owner control block for a pointer and copies its deleter state.</summary>
    Ps1TypedSharedControlBlock(TValue* value, const TDeleter& deleter)
        : Ps1SharedControlBlock{1, &Dispose, &Destroy},
          Value(value),
          Deleter(deleter) {
    }

    /// <summary>Invokes the stored deleter without releasing the control block yet.</summary>
    static void Dispose(Ps1SharedControlBlock* block) {
        Ps1TypedSharedControlBlock* typed = static_cast<Ps1TypedSharedControlBlock*>(block);
        typed->Deleter(typed->Value);
    }

    /// <summary>Releases the control block after its pointee has been disposed.</summary>
    static void Destroy(Ps1SharedControlBlock* block) {
        delete static_cast<Ps1TypedSharedControlBlock*>(block);
    }
};

}

namespace helengine::ps1 {

/// <summary>
/// Provides single-thread shared ownership for PS1 generated runtime buffers.
/// Copies share one control block, and the stored deleter runs exactly once when
/// the final copy is released. Instances are intended for one PS1 execution
/// thread; concurrent access requires an external synchronization policy.
/// </summary>
template <typename T>
class Ps1SharedPtr {
    template <typename>
    friend class Ps1SharedPtr;

public:
    /// <summary>Creates an empty owner.</summary>
    Ps1SharedPtr()
        : Value(nullptr),
          Control(nullptr) {
    }

    /// <summary>Creates an empty owner from null.</summary>
    Ps1SharedPtr(std::nullptr_t)
        : Value(nullptr),
          Control(nullptr) {
    }

    /// <summary>Creates one owner and stores a copy of its custom deleter.</summary>
    template <typename U, typename TDeleter, typename std::enable_if<std::is_convertible<U*, T*>::value, int>::type = 0>
    explicit Ps1SharedPtr(U* value, const TDeleter& deleter)
        : Value(value),
          Control(new detail::Ps1TypedSharedControlBlock<U, TDeleter>(value, deleter)) {
    }

    /// <summary>Shares ownership with another owner of the same type.</summary>
    Ps1SharedPtr(const Ps1SharedPtr& other)
        : Value(other.Value),
          Control(other.Control) {
        AddReference();
    }

    /// <summary>Shares ownership across a pointer conversion.</summary>
    template <typename U, typename std::enable_if<std::is_convertible<U*, T*>::value, int>::type = 0>
    Ps1SharedPtr(const Ps1SharedPtr<U>& other)
        : Value(other.Value),
          Control(other.Control) {
        AddReference();
    }

    /// <summary>Moves one owner without changing the reference count.</summary>
    Ps1SharedPtr(Ps1SharedPtr&& other)
        : Value(other.Value),
          Control(other.Control) {
        other.Value = nullptr;
        other.Control = nullptr;
    }

    /// <summary>Releases the owned reference.</summary>
    ~Ps1SharedPtr() {
        Release();
    }

    /// <summary>Shares ownership through copy assignment.</summary>
    Ps1SharedPtr& operator=(const Ps1SharedPtr& other) {
        if (this != &other) {
            Ps1SharedPtr copy(other);
            swap(copy);
        }
        return *this;
    }

    /// <summary>Moves ownership through move assignment.</summary>
    Ps1SharedPtr& operator=(Ps1SharedPtr&& other) {
        if (this != &other) {
            Release();
            Value = other.Value;
            Control = other.Control;
            other.Value = nullptr;
            other.Control = nullptr;
        }
        return *this;
    }

    /// <summary>Returns the observed pointer without transferring ownership.</summary>
    T* get() const {
        return Value;
    }

    /// <summary>Returns the number of owners sharing this control block.</summary>
    std::size_t use_count() const {
        return Control == nullptr ? 0 : Control->References;
    }

    /// <summary>Reports whether an observed pointer is present.</summary>
    explicit operator bool() const {
        return Value != nullptr;
    }

    /// <summary>Releases this owner and leaves it empty.</summary>
    void reset() {
        Release();
    }

    /// <summary>Replaces this owner with a new custom-deleter owner.</summary>
    template <typename U, typename TDeleter, typename std::enable_if<std::is_convertible<U*, T*>::value, int>::type = 0>
    void reset(U* value, const TDeleter& deleter) {
        Ps1SharedPtr replacement(value, deleter);
        swap(replacement);
    }

    /// <summary>Exchanges two owners without invoking either deleter.</summary>
    void swap(Ps1SharedPtr& other) {
        T* value = Value;
        Value = other.Value;
        other.Value = value;

        detail::Ps1SharedControlBlock* control = Control;
        Control = other.Control;
        other.Control = control;
    }

private:
    /// <summary>Adds one owner to the control block when it exists.</summary>
    void AddReference() {
        if (Control != nullptr) {
            ++Control->References;
        }
    }

    /// <summary>Releases one owner and destroys the pointee at the final release.</summary>
    void Release() {
        detail::Ps1SharedControlBlock* control = Control;
        Value = nullptr;
        Control = nullptr;
        if (control == nullptr) {
            return;
        }

        --control->References;
        if (control->References == 0) {
            control->DisposeObject(control);
            control->DestroyBlock(control);
        }
    }

    /// <summary>Pointer observed by this owner without owning a second copy.</summary>
    T* Value;

    /// <summary>Shared control block or null for an empty owner.</summary>
    detail::Ps1SharedControlBlock* Control;
};

}
