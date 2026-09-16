#include "platform/ps1/Ps1SharedPtr.hpp"

#include <utility>

struct CountingDelete {
    int* Calls;

    void operator()(int* value) const {
        if (value != nullptr && *value == 42) {
            ++*Calls;
        }
        delete value;
    }
};

int main() {
    int deletes = 0;
    {
        helengine::ps1::Ps1SharedPtr<int> owner(new int(42), CountingDelete{&deletes});
        if (owner.get() == nullptr || *owner.get() != 42 || owner.use_count() != 1) return 1;

        helengine::ps1::Ps1SharedPtr<int> copy(owner);
        helengine::ps1::Ps1SharedPtr<void> erased(copy);
        helengine::ps1::Ps1SharedPtr<int> moved(std::move(copy));
        if (copy.get() != nullptr || copy.use_count() != 0) return 2;
        if (owner.use_count() != 3 || erased.get() != static_cast<void*>(owner.get())) return 3;

        moved.reset();
        if (owner.use_count() != 2 || deletes != 0) return 4;
        erased.reset();
        if (owner.use_count() != 1 || deletes != 0) return 5;

        owner.reset();
        if (deletes != 1) return 6;
    }

    {
        helengine::ps1::Ps1SharedPtr<void> direct(new int(42), CountingDelete{&deletes});
        if (!direct || direct.use_count() != 1) return 7;
        helengine::ps1::Ps1SharedPtr<void> copy(direct);
        direct.reset();
        if (copy.use_count() != 1 || deletes != 1) return 8;
        copy.reset();
    }

    return deletes == 2 ? 0 : 9;
}
