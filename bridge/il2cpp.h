#pragma once
#include <stdint.h>
#include <stddef.h>
#include <dlfcn.h>
#include <mach-o/dyld.h>
#include <string>
#include <vector>
#include <unordered_map>

// ─── IL2CPP Opaque Types ──────────────────────────────────────────────────────
struct Il2CppObject { void* klass; void* monitor; };
struct Il2CppClass;
struct Il2CppImage;
struct Il2CppDomain;
struct Il2CppAssembly;
struct MethodInfo;
struct FieldInfo;

struct Il2CppArray {
    Il2CppObject obj;
    void*        bounds;
    uintptr_t    max_length;
    void*        vector[1];
};

// ─── IL2CPP Function Pointers ─────────────────────────────────────────────────
typedef Il2CppDomain*   (*pil2cpp_domain_get)();
typedef Il2CppAssembly**(*pil2cpp_domain_get_assemblies)(Il2CppDomain*, size_t*);
typedef Il2CppImage*    (*pil2cpp_assembly_get_image)(Il2CppAssembly*);
typedef Il2CppClass*    (*pil2cpp_class_from_name)(Il2CppImage*, const char*, const char*);
typedef MethodInfo*     (*pil2cpp_class_get_method_from_name)(Il2CppClass*, const char*, int);
typedef FieldInfo*      (*pil2cpp_class_get_field_from_name)(Il2CppClass*, const char*);
typedef void*           (*pil2cpp_method_get_object)(MethodInfo*, Il2CppClass*);
typedef void            (*pil2cpp_runtime_invoke)(MethodInfo*, void*, void**, void**);
typedef void*           (*pil2cpp_object_new)(Il2CppClass*);
typedef Il2CppArray*    (*pil2cpp_array_new)(Il2CppClass*, uintptr_t);
typedef void            (*pil2cpp_field_get_value)(Il2CppObject*, FieldInfo*, void*);
typedef void            (*pil2cpp_field_set_value)(Il2CppObject*, FieldInfo*, void*);
typedef Il2CppClass*    (*pil2cpp_object_get_class)(Il2CppObject*);
typedef bool            (*pil2cpp_class_is_assignable_from)(Il2CppClass*, Il2CppClass*);
typedef const char*     (*pil2cpp_class_get_name)(Il2CppClass*);
typedef size_t          (*pil2cpp_class_get_instance_size)(Il2CppClass*);

// ─── IL2CPP GC Object Walk (for findObjects) ─────────────────────────────────
typedef void (*GCObjectCallback)(Il2CppObject* obj, void* userdata);
typedef void (*pgc_foreach_heap)(GCObjectCallback cb, void* userdata);

// ─── Resolver ────────────────────────────────────────────────────────────────
class IL2CPP {
public:
    static IL2CPP& get() { static IL2CPP inst; return inst; }

    bool init(uintptr_t base) {
        _base = base;
        // Resolve via symbol export (libil2cpp.dylib)
        void* lib = dlopen("@rpath/UnityFramework.framework/UnityFramework", RTLD_NOLOAD | RTLD_NOW);
        if (!lib) lib = dlopen(nullptr, RTLD_NOW);
        if (!lib) return false;

#define LOAD(fn) fn##_ = (p##fn)dlsym(lib, #fn); if(!fn##_) return false
        LOAD(il2cpp_domain_get);
        LOAD(il2cpp_domain_get_assemblies);
        LOAD(il2cpp_assembly_get_image);
        LOAD(il2cpp_class_from_name);
        LOAD(il2cpp_class_get_method_from_name);
        LOAD(il2cpp_class_get_field_from_name);
        LOAD(il2cpp_runtime_invoke);
        LOAD(il2cpp_object_new);
        LOAD(il2cpp_array_new);
        LOAD(il2cpp_field_get_value);
        LOAD(il2cpp_field_set_value);
        LOAD(il2cpp_object_get_class);
        LOAD(il2cpp_class_is_assignable_from);
        LOAD(il2cpp_class_get_name);
#undef LOAD

        // GC walk — may not export, optional
        gc_foreach_heap_ = (pgc_foreach_heap)dlsym(lib, "il2cpp_gc_foreach_heap");

        // Cache all images
        size_t count = 0;
        Il2CppDomain* domain = il2cpp_domain_get_();
        Il2CppAssembly** assemblies = il2cpp_domain_get_assemblies_(domain, &count);
        for (size_t i = 0; i < count; i++) {
            _images.push_back(il2cpp_assembly_get_image_(assemblies[i]));
        }

        _ready = true;
        return true;
    }

    bool ready() const { return _ready; }

    Il2CppClass* classFromName(const std::string& fullName) {
        auto it = _classCache.find(fullName);
        if (it != _classCache.end()) return it->second;

        // Split "Namespace.ClassName" or just "ClassName"
        std::string ns, name;
        size_t dot = fullName.rfind('.');
        if (dot != std::string::npos) {
            ns   = fullName.substr(0, dot);
            name = fullName.substr(dot + 1);
        } else {
            ns   = "";
            name = fullName;
        }

        for (auto img : _images) {
            Il2CppClass* klass = il2cpp_class_from_name_(img, ns.c_str(), name.c_str());
            if (klass) {
                _classCache[fullName] = klass;
                return klass;
            }
        }
        // Try empty namespace if dotted name failed
        if (!ns.empty()) {
            for (auto img : _images) {
                Il2CppClass* klass = il2cpp_class_from_name_(img, "", fullName.c_str());
                if (klass) { _classCache[fullName] = klass; return klass; }
            }
        }
        return nullptr;
    }

    // Find all live instances of a class via GC heap walk
    std::vector<Il2CppObject*> findObjects(Il2CppClass* klass) {
        std::vector<Il2CppObject*> result;
        if (!klass || !gc_foreach_heap_) return result;

        struct Ctx { Il2CppClass* klass; std::vector<Il2CppObject*>* out; IL2CPP* self; };
        Ctx ctx { klass, &result, this };

        gc_foreach_heap_([](Il2CppObject* obj, void* ud) {
            if (!obj) return;
            Ctx* c = (Ctx*)ud;
            Il2CppClass* objClass = c->self->il2cpp_object_get_class_(obj);
            if (objClass && c->self->il2cpp_class_is_assignable_from_(c->klass, objClass)) {
                c->out->push_back(obj);
            }
        }, &ctx);

        return result;
    }

    // Invoke a void/value method by name
    void* invoke(Il2CppObject* obj, const char* method, void** args = nullptr, int argc = 0) {
        if (!obj) return nullptr;
        Il2CppClass* klass = il2cpp_object_get_class_(obj);
        if (!klass) return nullptr;
        MethodInfo* mi = il2cpp_class_get_method_from_name_(klass, method, argc);
        if (!mi) return nullptr;
        void* exc = nullptr;
        il2cpp_runtime_invoke_(mi, obj, args, &exc);
        return exc; // returns exception if any
    }

    // Public helper — dùng cho lua_bind.h
    Il2CppClass* getObjectClass(Il2CppObject* obj) {
        if (!obj || !il2cpp_object_get_class_) return nullptr;
        return il2cpp_object_get_class_(obj);
    }

    // Field helpers
    template<typename T>
    T getField(Il2CppObject* obj, const char* fieldName) {
        T out{};
        if (!obj) return out;
        Il2CppClass* klass = il2cpp_object_get_class_(obj);
        FieldInfo* fi = il2cpp_class_get_field_from_name_(klass, fieldName);
        if (fi) il2cpp_field_get_value_(obj, fi, &out);
        return out;
    }

    template<typename T>
    void setField(Il2CppObject* obj, const char* fieldName, T value) {
        if (!obj) return;
        Il2CppClass* klass = il2cpp_object_get_class_(obj);
        FieldInfo* fi = il2cpp_class_get_field_from_name_(klass, fieldName);
        if (fi) il2cpp_field_set_value_(obj, fi, &value);
    }

private:
    bool _ready = false;
    uintptr_t _base = 0;
    std::vector<Il2CppImage*> _images;
    std::unordered_map<std::string, Il2CppClass*> _classCache;

    pil2cpp_domain_get              il2cpp_domain_get_              = nullptr;
    pil2cpp_domain_get_assemblies   il2cpp_domain_get_assemblies_   = nullptr;
    pil2cpp_assembly_get_image      il2cpp_assembly_get_image_      = nullptr;
    pil2cpp_class_from_name         il2cpp_class_from_name_         = nullptr;
    pil2cpp_class_get_method_from_name il2cpp_class_get_method_from_name_ = nullptr;
    pil2cpp_class_get_field_from_name  il2cpp_class_get_field_from_name_  = nullptr;
    pil2cpp_runtime_invoke          il2cpp_runtime_invoke_          = nullptr;
    pil2cpp_object_new              il2cpp_object_new_              = nullptr;
    pil2cpp_array_new               il2cpp_array_new_               = nullptr;
    pil2cpp_field_get_value         il2cpp_field_get_value_         = nullptr;
    pil2cpp_field_set_value         il2cpp_field_set_value_         = nullptr;
    pil2cpp_object_get_class        il2cpp_object_get_class_        = nullptr;
    pil2cpp_class_is_assignable_from il2cpp_class_is_assignable_from_ = nullptr;
    pil2cpp_class_get_name          il2cpp_class_get_name_          = nullptr;
    pgc_foreach_heap                gc_foreach_heap_                = nullptr;
};
