/**
 * @file type_erased.hpp
 * @brief Generic type erasure template with small buffer optimization
 * @author dorgby.net
 * @copyright Copyright (c) 2025 dorgby.net. Licensed under MIT License, see LICENSE for details.
 */
#pragma once

#include <cstddef>
#include <utility>
#include <cstring>
#include <cassert>
#include <concepts>
#include <type_traits>

namespace slwoggy
{

/**
 * @brief CRTP helper to provide default implementations of type erasure operations
 *
 * This helper class eliminates boilerplate by providing standard implementations
 * of clone_in_place, move_in_place, and heap_clone for model types.
 *
 * Best suited for simple wrapper models that:
 * - Store a single wrapped object
 * - Have standard copy/move semantics
 * - Don't need custom cloning behavior
 *
 * @note For more complex models with multiple members or special cloning needs,
 * you may need to implement these methods manually.
 *
 * @tparam Derived The derived model class
 * @tparam Base The concept interface class
 */
template <typename Derived, typename Base> struct type_erasure_helper : Base
{
    Base *clone_in_place(void *buf) const override { return new (buf) Derived(static_cast<const Derived &>(*this)); }

    Base *move_in_place(void *buf) noexcept override
    {
        return new (buf) Derived(std::move(static_cast<Derived &>(*this)));
    }

    Base *heap_clone() const override { return new Derived(static_cast<const Derived &>(*this)); }
};

/**
 * @brief Generic type erasure template with small buffer optimization
 *
 * This template provides a reusable type erasure pattern that can store any object
 * implementing the ConceptT interface. It uses small buffer optimization to avoid
 * heap allocations for objects that fit within the buffer.
 *
 * ## Key Features:
 * - Small Buffer Optimization (SBO) for objects that fit in the buffer
 * - Strong exception safety via copy-and-swap idiom
 * - Zero overhead for small objects (no heap allocation)
 * - Type safety through concepts and static assertions
 * - Alignment-aware storage allocation
 *
 * ## Requirements for ConceptT:
 * ConceptT must provide the following virtual methods:
 * - `clone_in_place(void*) const` - Copy construct into provided buffer
 * - `move_in_place(void*) noexcept` - Move construct into provided buffer
 * - `heap_clone() const` - Create a heap-allocated copy
 *
 * ## Design Decisions:
 * - **Move semantics**: For small objects, we call destroy() on the moved-from
 *   object immediately. This ensures a clean, predictable empty state at the cost
 *   of running the destructor early. An alternative would be to just set ptr_ to
 *   nullptr for a micro-optimization, but we prefer the cleaner semantics.
 *
 * Note: Use the provided `type_erasure_helper` CRTP base class to automatically
 * implement these methods and eliminate boilerplate. See examples below.
 *
 * ## Usage Example:
 *
 * @code
 * // Define the concept interface
 * struct drawable_concept {
 * virtual ~drawable_concept() = default;
 * virtual void draw() const = 0;
 * // The following are required for type erasure but can be boilerplate.
 * virtual drawable_concept* clone_in_place(void* buf) const = 0;
 * virtual drawable_concept* move_in_place(void* buf) noexcept = 0;
 * virtual drawable_concept* heap_clone() const = 0;
 * };
 *
 * // Concrete implementation using the helper to eliminate boilerplate
 * template<typename Shape>
 * struct drawable_model : type_erasure_helper<drawable_model<Shape>, drawable_concept> {
 * Shape shape_;
 * * explicit drawable_model(Shape s) : shape_(std::move(s)) {}
 * * void draw() const override { shape_.draw(); }
 * // clone_in_place, move_in_place, and heap_clone are provided by the helper!
 * };
 *
 * // Use the type erasure
 * using drawable = type_erased<drawable_concept, 64>;
 *
 * struct Circle { void draw() const {  ...  } };
 * struct Rectangle { void draw() const {  ...  } };
 *
 * drawable d1{drawable_model<Circle>{Circle{}}};
 * drawable d2{drawable_model<Rectangle>{Rectangle{}}};
 * * d1->draw();  // Calls Circle::draw()
 * d2->draw();  // Calls Rectangle::draw()
 * @endcode
 *
 * ## Advanced Example - Function Wrapper:
 * @code
 * // Concept for callable objects
 * struct callable_concept {
 *     virtual ~callable_concept() = default;
 *     virtual void call() = 0;
 *     virtual callable_concept* clone_in_place(void* buf) const = 0;
 *     virtual callable_concept* move_in_place(void* buf) noexcept = 0;
 *     virtual callable_concept* heap_clone() const = 0;
 * };
 *
 * // Model for specific callables - WITHOUT using the helper
 * template<typename F>
 * struct callable_model : callable_concept {
 *     F func_;
 *
 *     explicit callable_model(F f) : func_(std::move(f)) {}
 *
 *     void call() override { func_(); }
 *
 *     // Boilerplate implementations:
 *     callable_concept* clone_in_place(void* buf) const override {
 *         return new (buf) callable_model(func_);
 *     }
 *
 *     callable_concept* move_in_place(void* buf) noexcept override {
 *         return new (buf) callable_model(std::move(func_));
 *     }
 *
 *     callable_concept* heap_clone() const override {
 *         return new callable_model(func_);
 *     }
 * };
 *
 * // Alternative: Model using the type_erasure_helper to eliminate boilerplate
 * template<typename F>
 * struct callable_model_v2 : type_erasure_helper<callable_model_v2<F>, callable_concept> {
 *     F func_;
 *
 *     explicit callable_model_v2(F f) : func_(std::move(f)) {}
 *
 *     void call() override { func_(); }
 *     // No need to implement clone_in_place, move_in_place, or heap_clone!
 * };
 *
 * // Type alias for convenience
 * using function = type_erased<callable_concept, 48>;
 *
 * // Usage
 * function f1{callable_model{[]{ std::cout << "Lambda\n"; }}};
 *
 * void regular_function() { std::cout << "Function\n"; }
 * function f2{callable_model{&regular_function}};
 *
 * struct Functor {
 *     int value;
 *     void operator()() { std::cout << "Functor: " << value << "\n"; }
 * };
 * function f3{callable_model{Functor{42}}};
 *
 * // All can be called uniformly
 * f1->call();  // Output: Lambda
 * f2->call();  // Output: Function
 * f3->call();  // Output: Functor: 42
 * @endcode
 *
 * ## Example - STL Container Usage:
 * @code
 * // Using type_erased in STL containers to store heterogeneous objects
 * #include <vector>
 * #include <algorithm>
 *
 * // Animal hierarchy example
 * struct animal_concept {
 *     virtual ~animal_concept() = default;
 *     virtual std::string speak() const = 0;
 *     virtual int age() const = 0;
 *     virtual animal_concept* clone_in_place(void* buf) const = 0;
 *     virtual animal_concept* move_in_place(void* buf) noexcept = 0;
 *     virtual animal_concept* heap_clone() const = 0;
 * };
 *
 * // Using the type_erasure_helper to eliminate boilerplate
 * template<typename Animal>
 * struct animal_model : type_erasure_helper<animal_model<Animal>, animal_concept> {
 *     Animal animal_;
 *
 *     explicit animal_model(Animal a) : animal_(std::move(a)) {}
 *
 *     std::string speak() const override { return animal_.speak(); }
 *     int age() const override { return animal_.age(); }
 *     // No need to implement clone_in_place, move_in_place, or heap_clone!
 * };
 *
 * using animal = type_erased<animal_concept, 32>;
 *
 * // Concrete animal types
 * struct Dog {
 *     int age_ = 5;
 *     std::string speak() const { return "Woof!"; }
 *     int age() const { return age_; }
 * };
 *
 * struct Cat {
 *     int age_ = 3;
 *     std::string speak() const { return "Meow!"; }
 *     int age() const { return age_; }
 * };
 *
 * struct Parrot {
 *     int age_ = 15;
 *     std::string phrase_ = "Hello!";
 *     std::string speak() const { return phrase_; }
 *     int age() const { return age_; }
 * };
 *
 * // Usage in STL containers
 * std::vector<animal> zoo;
 *
 * // Add different animals
 * zoo.emplace_back(animal_model<Dog>{Dog{}});
 * zoo.emplace_back(animal_model<Cat>{Cat{}});
 * zoo.emplace_back(animal_model<Parrot>{Parrot{20, "Polly wants a cracker!"}});
 *
 * // Iterate over all animals
 * for (const auto& a : zoo) {
 *     std::cout << a->speak() << " (age: " << a->age() << ")\n";
 * }
 * // Output:
 * // Woof! (age: 5)
 * // Meow! (age: 3)
 * // Polly wants a cracker! (age: 20)
 *
 * // Use STL algorithms
 * auto oldest = std::max_element(zoo.begin(), zoo.end(),
 *     [](const animal& a, const animal& b) {
 *         return a->age() < b->age();
 *     });
 * std::cout << "Oldest animal says: " << (*oldest)->speak() << "\n";
 * // Output: Oldest animal says: Polly wants a cracker!
 *
 * // Copy the container (tests copy semantics)
 * std::vector<animal> zoo_copy = zoo;
 *
 * // Move animals around (tests move semantics)
 * std::vector<animal> new_zoo = std::move(zoo);
 *
 * // Sort by age
 * std::sort(new_zoo.begin(), new_zoo.end(),
 *     [](const animal& a, const animal& b) {
 *         return a->age() < b->age();
 *     });
 *
 * // Benefits of type_erased in containers:
 * // 1. Store different types in the same container without inheritance
 * // 2. Value semantics - no need for pointers or smart pointers
 * // 3. Small objects stay on the stack (cache-friendly)
 * // 4. Works seamlessly with STL algorithms
 * // 5. Move semantics for efficient container operations
 * @endcode
 *
 * @tparam ConceptT The abstract interface that concrete types must implement
 * @tparam BufferSize Size of the inline storage buffer for small object optimization
 */
template <typename ConceptT, size_t BufferSize = 64> class type_erased
{
    static constexpr size_t buffer_size = BufferSize;

    // Ensure ConceptT has a virtual destructor for safe polymorphic deletion
    static_assert(std::has_virtual_destructor_v<ConceptT>,
                  "ConceptT must have a virtual destructor for safe polymorphic deletion");

    // Verify ConceptT has required methods for type erasure
    static_assert(
        requires(const ConceptT &c, void *buf) {
            { c.clone_in_place(buf) } -> std::same_as<ConceptT *>;
            { c.heap_clone() } -> std::same_as<ConceptT *>;
        },
        "ConceptT must support clone_in_place(void*) and heap_clone()");

    static_assert(
        requires(ConceptT &c, void *buf) {
            { c.move_in_place(buf) } -> std::same_as<ConceptT *>;
        },
        "ConceptT must support move_in_place(void*)");

    // Ensure move_in_place is noexcept for swap to be noexcept
    static_assert(noexcept(std::declval<ConceptT &>().move_in_place(nullptr)),
                  "ConceptT::move_in_place must be noexcept for swap to be noexcept");

    // Ensure buffer is at least large enough for the base concept
    static_assert(BufferSize >= sizeof(ConceptT), "BufferSize too small for ConceptT base class");

    // Helper to check if a type fits in small buffer (size AND alignment)
    template <typename Model>
    static constexpr bool fits_small_buffer = sizeof(Model) <= buffer_size && alignof(Model) <= alignof(std::max_align_t);

    // Storage and state
    alignas(std::max_align_t) char storage_[buffer_size];
    ConceptT *ptr_ = nullptr;
    bool is_small_ = false;

    void destroy() noexcept
    {
        if (ptr_)
        {
            is_small_ ? ptr_->~ConceptT() : delete ptr_;
            ptr_ = nullptr;
        }
    }

  public:
    // Default constructor
    type_erased() = default;

    // Constructor from concrete model
    template <typename ModelT>
        requires std::is_base_of_v<ConceptT, ModelT>
    explicit type_erased(ModelT &&model)
    {
        using Model = std::decay_t<ModelT>;

        if constexpr (fits_small_buffer<Model>)
        {
            ptr_      = new (storage_) Model(std::forward<ModelT>(model));
            is_small_ = true;
        }
        else
        {
            ptr_      = new Model(std::forward<ModelT>(model));
            is_small_ = false;
        }
    }

    // Copy constructor
    type_erased(const type_erased &other) : is_small_(other.is_small_)
    {
        if (other.ptr_) { ptr_ = is_small_ ? other.ptr_->clone_in_place(storage_) : other.ptr_->heap_clone(); }
    }

    // Move constructor
    type_erased(type_erased &&other) noexcept : is_small_(other.is_small_), ptr_(nullptr)
    {
        if (other.ptr_)
        {
            if (is_small_)
            {
                // Small object: must move-construct into our storage
                ptr_ = other.ptr_->move_in_place(storage_);
                // Note: We call destroy() to immediately clean up the moved-from object.
                // Alternative: Just set other.ptr_ = nullptr for a micro-optimization,
                // leaving the moved-from object in storage until other's destructor runs.
                // We chose the cleaner approach for more predictable behavior.
                other.destroy();
            }
            else
            {
                // Large object: just steal the pointer
                ptr_       = other.ptr_;
                other.ptr_ = nullptr;
            }
        }
    }

    // Copy assignment - using copy-and-swap idiom for strong exception safety
    type_erased &operator=(const type_erased &other)
    {
        type_erased temp(other);
        swap(temp);
        return *this;
    }

    // Move assignment
    type_erased &operator=(type_erased &&other) noexcept
    {
        if (this != &other)
        {
            destroy();
            is_small_ = other.is_small_;
            if (other.ptr_)
            {
                if (is_small_)
                {
                    ptr_ = other.ptr_->move_in_place(storage_);
                    // Same design decision as move constructor - see comment there
                    other.destroy();
                }
                else
                {
                    ptr_       = other.ptr_;
                    other.ptr_ = nullptr;
                }
            }
        }
        return *this;
    }

    ~type_erased() { destroy(); }

    // Emplace construction
    template <typename ModelT, typename... Args>
        requires std::is_base_of_v<ConceptT, ModelT>
    void emplace(Args &&...args)
    {
        destroy();

        if constexpr (fits_small_buffer<ModelT>)
        {
            ptr_      = new (storage_) ModelT(std::forward<Args>(args)...);
            is_small_ = true;
        }
        else
        {
            ptr_      = new ModelT(std::forward<Args>(args)...);
            is_small_ = false;
        }
    }

    // Access to the stored object
    ConceptT *operator->()
    {
        assert(ptr_ && "Dereferencing empty type_erased");
        return ptr_;
    }
    const ConceptT *operator->() const
    {
        assert(ptr_ && "Dereferencing empty type_erased");
        return ptr_;
    }
    ConceptT &operator*()
    {
        assert(ptr_ && "Dereferencing empty type_erased");
        return *ptr_;
    }
    const ConceptT &operator*() const
    {
        assert(ptr_ && "Dereferencing empty type_erased");
        return *ptr_;
    }

    // Check if empty
    bool empty() const { return ptr_ == nullptr; }
    explicit operator bool() const { return ptr_ != nullptr; }

    // Swap operation
    void swap(type_erased &other) noexcept
    {
        // If this object is empty, we can just move from the other object
        if (!ptr_)
        {
            if (other.ptr_)
            { // If other is not empty
                is_small_ = other.is_small_;
                if (is_small_) { ptr_ = other.ptr_->move_in_place(storage_); }
                else { ptr_ = other.ptr_; }
                // Leave other in a valid, empty state
                other.ptr_      = nullptr;
                other.is_small_ = false;
            }
            return;
        }

        // If the other object is empty, have it move from this one
        if (!other.ptr_)
        {
            other.swap(*this); // Recursively call swap with roles reversed
            return;
        }

        // --- From this point, both objects are guaranteed to be non-empty ---

        if (is_small_ && other.is_small_) // Case 1: Both are small
        {
            // Use a temporary stack buffer to perform a three-way move
            alignas(std::max_align_t) char temp_storage[buffer_size];
            ptr_->move_in_place(temp_storage);
            other.ptr_->move_in_place(storage_);
            reinterpret_cast<ConceptT *>(temp_storage)->move_in_place(other.storage_);
        }
        else if (!is_small_ && !other.is_small_) // Case 2: Both are large
        {
            // The simplest case: just swap the heap pointers
            std::swap(ptr_, other.ptr_);
        }
        else // Case 3: Mixed states (one small, one large)
        {
            type_erased *small_obj = is_small_ ? this : &other;
            type_erased *large_obj = is_small_ ? &other : this;

            // Hold the large object's heap pointer
            ConceptT *temp_large_ptr = large_obj->ptr_;
            large_obj->ptr_          = nullptr; // Prevent aliasing issues

            // Move the small object's contents into the large object's buffer
            // The large object is now becoming the small one
            small_obj->ptr_->move_in_place(large_obj->storage_);
            large_obj->ptr_ = reinterpret_cast<ConceptT *>(large_obj->storage_);

            // The small object can now steal the heap pointer
            small_obj->ptr_ = temp_large_ptr;

            // Finally, swap the state flags
            std::swap(is_small_, other.is_small_);
        }
    }
};

// Non-member swap to match standard library idioms
template <typename ConceptT, size_t BufferSize>
void swap(type_erased<ConceptT, BufferSize> &lhs, type_erased<ConceptT, BufferSize> &rhs) noexcept
{
    lhs.swap(rhs);
}

} // namespace slwoggy