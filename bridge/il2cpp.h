#pragma once
#include <stdint.h>
#include <stddef.h>
#include <dlfcn.h>
#include <mach-o/dyld.h>
#include <string.h>
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
typedef Il2CppObject*   (*pil2cpp_runtime_invoke)(const MethodInfo*, void*, void**, void**);
typedef void*           (*pil2cpp_object_new)(Il2CppClass*);
typedef Il2CppArray*    (*pil2cpp_array_new)(Il2CppClass*, uintptr_t);
typedef void            (*pil2cpp_field_get_value)(Il2CppObject*, FieldInfo*, void*);
typedef void            (*pil2cpp_field_set_value)(Il2CppObject*, FieldInfo*, void*);
typedef Il2CppClass*    (*pil2cpp_object_get_class)(Il2CppObject*);
typedef bool            (*pil2cpp_class_is_assignable_from)(Il2CppClass*, Il2CppClass*);
typedef const char*     (*pil2cpp_class_get_name)(Il2CppClass*);
typedef const char*     (*pil2cpp_class_get_namespace)(Il2CppClass*);
typedef const char*     (*pil2cpp_image_get_name)(Il2CppImage*);
typedef size_t          (*pil2cpp_image_get_class_count)(Il2CppImage*);
typedef Il2CppClass*    (*pil2cpp_image_get_class)(Il2CppImage*, size_t);
typedef const void*     (*pil2cpp_class_get_type)(Il2CppClass*);
typedef Il2CppObject*   (*pil2cpp_type_get_object)(const void*);

// GC walk (optional — usually not exported on iOS)
typedef void (*GCObjectCallback)(Il2CppObject* obj, void* userdata);
typedef void (*pgc_foreach_heap)(GCObjectCallback cb, void* userdata);

// ─── Resolver ────────────────────────────────────────────────────────────────
class IL2CPP {
public:
    static IL2CPP& get() { static IL2CPP inst; return inst; }

    bool init(uintptr_t base) {
        _base = base;
        void* lib = dlopen("@rpath/UnityFramework.framework/UnityFramework", RTLD_NOLOAD | RTLD_NOW);
        if (!lib) lib = dlopen(nullptr, RTLD_NOW);
        if (!lib) return false;
        _lib = lib;

#define LOAD_REQ(fn) fn##_ = (p##fn)dlsym(lib, #fn); if(!fn##_) return false
#define LOAD_OPT(fn) fn##_ = (p##fn)dlsym(lib, #fn)
        LOAD_REQ(il2cpp_domain_get);
        LOAD_REQ(il2cpp_domain_get_assemblies);
        LOAD_REQ(il2cpp_assembly_get_image);
        LOAD_REQ(il2cpp_class_from_name);
        LOAD_REQ(il2cpp_class_get_method_from_name);
        LOAD_REQ(il2cpp_class_get_field_from_name);
        LOAD_REQ(il2cpp_runtime_invoke);
        LOAD_REQ(il2cpp_object_new);
        LOAD_REQ(il2cpp_array_new);
        LOAD_REQ(il2cpp_field_get_value);
        LOAD_REQ(il2cpp_field_set_value);
        LOAD_REQ(il2cpp_object_get_class);
        LOAD_REQ(il2cpp_class_is_assignable_from);
        LOAD_REQ(il2cpp_class_get_name);

        LOAD_OPT(il2cpp_class_get_namespace);
        LOAD_OPT(il2cpp_image_get_name);
        LOAD_OPT(il2cpp_image_get_class_count);
        LOAD_OPT(il2cpp_image_get_class);
        LOAD_OPT(il2cpp_class_get_type);
        LOAD_OPT(il2cpp_type_get_object);
#undef LOAD_REQ
#undef LOAD_OPT

        gc_foreach_heap_ = (pgc_foreach_heap)dlsym(lib, "il2cpp_gc_foreach_heap");

        refreshAssemblies();
        _ready = true;
        return true;
    }

    bool ready() const { return _ready; }

    // Refresh assembly/image cache — gọi định kỳ để bắt HybridCLR hot-update DLLs
    size_t refreshAssemblies() {
        if (!il2cpp_domain_get_ || !il2cpp_domain_get_assemblies_) return 0;
        size_t count = 0;
        Il2CppDomain* domain = il2cpp_domain_get_();
        Il2CppAssembly** assemblies = il2cpp_domain_get_assemblies_(domain, &count);
        _images.clear();
        for (size_t i = 0; i < count; i++) {
            Il2CppImage* img = il2cpp_assembly_get_image_(assemblies[i]);
            if (img) _images.push_back(img);
        }
        return _images.size();
    }

    size_t assemblyCount() {
        refreshAssemblies();
        return _images.size();
    }

    // Lấy danh sách tên của tất cả image (DLL) đang load
    std::vector<std::string> listAssemblies() {
        std::vector<std::string> names;
        refreshAssemblies();
        if (!il2cpp_image_get_name_) return names;
        for (auto img : _images) {
            const char* n = il2cpp_image_get_name_(img);
            names.push_back(n ? n : "?");
        }
        return names;
    }

    // Liệt kê class có tên chứa pattern (substring, case-insensitive)
    std::vector<std::string> findClassesByPattern(const std::string& pattern, int maxResults = 200) {
        std::vector<std::string> out;
        refreshAssemblies();
        if (!il2cpp_image_get_class_count_ || !il2cpp_image_get_class_) return out;

        std::string patLow = pattern;
        for (auto& c : patLow) c = (char)tolower(c);

        for (auto img : _images) {
            size_t n = il2cpp_image_get_class_count_(img);
            for (size_t i = 0; i < n; i++) {
                Il2CppClass* k = il2cpp_image_get_class_(img, i);
                if (!k) continue;
                const char* name = il2cpp_class_get_name_(k);
                if (!name) continue;
                std::string lname = name;
                for (auto& c : lname) c = (char)tolower(c);
                if (patLow.empty() || lname.find(patLow) != std::string::npos) {
                    std::string full;
                    if (il2cpp_class_get_namespace_) {
                        const char* ns = il2cpp_class_get_namespace_(k);
                        if (ns && ns[0]) { full = ns; full += "."; }
                    }
                    full += name;
                    out.push_back(full);
                    if ((int)out.size() >= maxResults) return out;
                }
            }
        }
        return out;
    }

    // Tìm class qua các biến thể namespace
    Il2CppClass* classFromName(const std::string& fullName) {
        auto it = _classCache.find(fullName);
        if (it != _classCache.end()) return it->second;

        std::string ns, name;
        size_t dot = fullName.rfind('.');
        if (dot != std::string::npos) {
            ns   = fullName.substr(0, dot);
            name = fullName.substr(dot + 1);
        } else {
            ns   = "";
            name = fullName;
        }

        // 1) Thử với (ns, name)
        for (auto img : _images) {
            Il2CppClass* k = il2cpp_class_from_name_(img, ns.c_str(), name.c_str());
            if (k) { _classCache[fullName] = k; return k; }
        }
        // 2) Thử empty ns + full name (ví dụ tên chứa dấu chấm bên trong)
        if (!ns.empty()) {
            for (auto img : _images) {
                Il2CppClass* k = il2cpp_class_from_name_(img, "", fullName.c_str());
                if (k) { _classCache[fullName] = k; return k; }
            }
        }
        // 3) Thử empty ns + tên cuối (cho class không có namespace)
        for (auto img : _images) {
            Il2CppClass* k = il2cpp_class_from_name_(img, "", name.c_str());
            if (k) { _classCache[fullName] = k; return k; }
        }

        // 4) Refresh và thử lại (HybridCLR có thể load thêm DLL)
        size_t before = _images.size();
        refreshAssemblies();
        if (_images.size() > before) {
            for (auto img : _images) {
                Il2CppClass* k = il2cpp_class_from_name_(img, ns.c_str(), name.c_str());
                if (k) { _classCache[fullName] = k; return k; }
                k = il2cpp_class_from_name_(img, "", name.c_str());
                if (k) { _classCache[fullName] = k; return k; }
            }
        }

        // 5) Fallback: scan mọi class trong mọi image, so tên
        if (il2cpp_image_get_class_count_ && il2cpp_image_get_class_) {
            for (auto img : _images) {
                size_t n = il2cpp_image_get_class_count_(img);
                for (size_t i = 0; i < n; i++) {
                    Il2CppClass* k = il2cpp_image_get_class_(img, i);
                    if (!k) continue;
                    const char* cname = il2cpp_class_get_name_(k);
                    if (cname && name == cname) {
                        _classCache[fullName] = k;
                        return k;
                    }
                }
            }
        }
        return nullptr;
    }

    // Tìm tất cả instance — ưu tiên UnityEngine.Object.FindObjectsOfType
    std::vector<Il2CppObject*> findObjects(Il2CppClass* klass) {
        std::vector<Il2CppObject*> result;
        if (!klass) return result;

        // Way 1: Resources.FindObjectsOfTypeAll(Type) — most reliable
        if (tryFindViaResources(klass, result) && !result.empty()) return result;

        // Way 2: Object.FindObjectsOfType(Type)
        if (tryFindViaObject(klass, result) && !result.empty()) return result;

        // Way 3: GC heap walk (rarely available on iOS)
        if (gc_foreach_heap_) {
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
        }
        return result;
    }

    void* invoke(Il2CppObject* obj, const char* method, void** args = nullptr, int argc = 0) {
        if (!obj) return nullptr;
        Il2CppClass* klass = il2cpp_object_get_class_(obj);
        if (!klass) return nullptr;
        MethodInfo* mi = il2cpp_class_get_method_from_name_(klass, method, argc);
        if (!mi) return nullptr;
        void* exc = nullptr;
        il2cpp_runtime_invoke_(mi, obj, args, &exc);
        return exc;
    }

    Il2CppClass* getObjectClass(Il2CppObject* obj) {
        if (!obj || !il2cpp_object_get_class_) return nullptr;
        return il2cpp_object_get_class_(obj);
    }

    template<typename T>
    T getField(Il2CppObject* obj, const char* fieldName) {
        T out{};
        if (!obj) return out;
        Il2CppClass* klass = il2cpp_object_get_class_(obj);
        if (!klass) return out;
        FieldInfo* fi = il2cpp_class_get_field_from_name_(klass, fieldName);
        if (fi) il2cpp_field_get_value_(obj, fi, &out);
        return out;
    }

    template<typename T>
    void setField(Il2CppObject* obj, const char* fieldName, T value) {
        if (!obj) return;
        Il2CppClass* klass = il2cpp_object_get_class_(obj);
        if (!klass) return;
        FieldInfo* fi = il2cpp_class_get_field_from_name_(klass, fieldName);
        if (fi) il2cpp_field_set_value_(obj, fi, &value);
    }

private:
    bool tryFindViaResources(Il2CppClass* klass, std::vector<Il2CppObject*>& out) {
        if (!il2cpp_class_get_type_ || !il2cpp_type_get_object_) return false;
        if (!_resFindAllMethod) {
            Il2CppClass* resCls = classFromName("UnityEngine.Resources");
            if (!resCls) return false;
            _resFindAllMethod = il2cpp_class_get_method_from_name_(resCls, "FindObjectsOfTypeAll", 1);
            if (!_resFindAllMethod) return false;
        }
        const void* type = il2cpp_class_get_type_(klass);
        Il2CppObject* typeObj = type ? il2cpp_type_get_object_(type) : nullptr;
        if (!typeObj) return false;
        void* args[] = { typeObj };
        void* exc = nullptr;
        Il2CppObject* ret = il2cpp_runtime_invoke_(_resFindAllMethod, nullptr, args, &exc);
        if (!ret) return false;
        Il2CppArray* arr = (Il2CppArray*)ret;
        for (uintptr_t i = 0; i < arr->max_length; i++) {
            Il2CppObject* o = ((Il2CppObject**)arr->vector)[i];
            if (o) out.push_back(o);
        }
        return true;
    }
    bool tryFindViaObject(Il2CppClass* klass, std::vector<Il2CppObject*>& out) {
        if (!il2cpp_class_get_type_ || !il2cpp_type_get_object_) return false;
        if (!_objFindMethod) {
            Il2CppClass* objCls = classFromName("UnityEngine.Object");
            if (!objCls) return false;
            _objFindMethod = il2cpp_class_get_method_from_name_(objCls, "FindObjectsOfType", 1);
            if (!_objFindMethod) return false;
        }
        const void* type = il2cpp_class_get_type_(klass);
        Il2CppObject* typeObj = type ? il2cpp_type_get_object_(type) : nullptr;
        if (!typeObj) return false;
        void* args[] = { typeObj };
        void* exc = nullptr;
        Il2CppObject* ret = il2cpp_runtime_invoke_(_objFindMethod, nullptr, args, &exc);
        if (!ret) return false;
        Il2CppArray* arr = (Il2CppArray*)ret;
        for (uintptr_t i = 0; i < arr->max_length; i++) {
            Il2CppObject* o = ((Il2CppObject**)arr->vector)[i];
            if (o) out.push_back(o);
        }
        return true;
    }

    bool      _ready = false;
    uintptr_t _base  = 0;
    void*     _lib   = nullptr;
    std::vector<Il2CppImage*> _images;
    std::unordered_map<std::string, Il2CppClass*> _classCache;

    MethodInfo* _resFindAllMethod = nullptr;
    MethodInfo* _objFindMethod    = nullptr;

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
    pil2cpp_class_get_namespace     il2cpp_class_get_namespace_     = nullptr;
    pil2cpp_image_get_name          il2cpp_image_get_name_          = nullptr;
    pil2cpp_image_get_class_count   il2cpp_image_get_class_count_   = nullptr;
    pil2cpp_image_get_class         il2cpp_image_get_class_         = nullptr;
    pil2cpp_class_get_type          il2cpp_class_get_type_          = nullptr;
    pil2cpp_type_get_object         il2cpp_type_get_object_         = nullptr;
    pgc_foreach_heap                gc_foreach_heap_                = nullptr;
};
