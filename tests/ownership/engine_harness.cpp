/*
 * Synthetic 4D engine boundary for ownership regressions.
 *
 * Links the real plugin sources (4DPlugin-Environment.cpp, the dormant,
 * unbuilt support/4DPlugin-JSON.cpp, C_TEXT.cpp) and the bundled, unmodified
 * 4D Plugin API (4DPluginAPI.c). Only gCall4D is replaced: it models the
 * engine-side allocations (Unistrings, pictures, objects, handles) with
 * malloc/free so ASan reports double frees / use-after-free, and keeps a
 * live set of every engine resource so leaks are detected exactly.
 *
 * Engine semantics modelled (per the SDK contract):
 *  - PA_CreateVariable(eVK_Unistring/eVK_Picture) allocates an empty value.
 *  - PA_SetObjectProperty deep-copies the variable (strings, pictures) or
 *    retains (objects, collections); the caller still owns its variable.
 *  - PA_GetObjectProperty returns an owned deep copy that must be cleared;
 *    on error the returned variable is left uninitialised (poisoned here).
 *  - PA_ClearVariable releases what the variable owns.
 */

#include "4DPluginAPI.h"
#include "PrivateTypes.h"
#include "EntryPoints.h"
#include "4DPlugin-JSON.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <map>
#include <set>
#include <string>
#include <functional>

extern "C" Call4DProcPtr gCall4D;

namespace {

typedef std::basic_string<PA_Unichar> u16;

struct Obj {
    int rc;
    std::map<u16, PA_Variable> props;
};

std::set<void *> g_strings;
std::set<void *> g_pictures;
std::set<Obj *> g_objects;
std::set<void *> g_handles;
bool g_fail_get = false;
bool g_fail_stringify = false;
int g_get_calls = 0;
/* Stands in for the uninitialised result of a failed PA_GetObjectProperty:
   readable (so the test fails by assertion, not by crashing) but never
   engine-owned, so clearing it is reported as a violation. */
PA_Unichar kPoisonBuf[] = { 'B', 'A', 'D', '!', 0 };
PA_Unichar *const kPoison = kPoisonBuf;

[[noreturn]] void die(const char *msg) {
    fprintf(stderr, "ENGINE VIOLATION: %s\n", msg);
    fflush(stderr);
    abort();
}

PA_Unistring new_string(const PA_Unichar *src, size_t len) {
    PA_Unichar *buf = (PA_Unichar *)malloc((len + 1) * sizeof(PA_Unichar));
    if(len) memcpy(buf, src, len * sizeof(PA_Unichar));
    buf[len] = 0;
    g_strings.insert(buf);
    PA_Unistring s;
    s.fLength = (PA_long32)len;
    s.fString = buf;
    s.fReserved1 = 0;
    s.fReserved2 = 0;
    return s;
}

void free_string(PA_Unistring *s) {
    if(!s->fString) return;
    if(s->fString == kPoison) die("disposed a string from a failed PA_GetObjectProperty");
    if(!g_strings.erase(s->fString)) die("disposed a string the engine does not own");
    free(s->fString);
    s->fString = 0;
    s->fLength = 0;
}

void *new_picture() {
    void *p = malloc(16);
    g_pictures.insert(p);
    return p;
}

void free_picture(void *p) {
    if(!p) return;
    if(!g_pictures.erase(p)) die("disposed a picture the engine does not own");
    free(p);
}

void release_object(Obj *o);

void clear_var(PA_Variable *v) {
    switch(v->fType) {
        case eVK_Unistring: free_string(&v->uValue.fString); break;
        case eVK_Picture: free_picture(v->uValue.fPicture); break;
        case eVK_Object: if(v->uValue.fObject) release_object((Obj *)v->uValue.fObject); break;
        case eVK_Collection: if(v->uValue.fCollection) release_object((Obj *)v->uValue.fCollection); break;
        default: break;
    }
    v->fType = eVK_Undefined;
}

PA_Variable copy_var(const PA_Variable &src) {
    PA_Variable dst = src;
    switch(src.fType) {
        case eVK_Unistring:
            dst.uValue.fString = new_string(src.uValue.fString.fString, src.uValue.fString.fLength);
            break;
        case eVK_Picture:
            dst.uValue.fPicture = new_picture();
            break;
        case eVK_Object:
        case eVK_Collection: {
            Obj *o = (Obj *)(src.fType == eVK_Object ? (void *)src.uValue.fObject : (void *)src.uValue.fCollection);
            if(o) {
                if(!g_objects.count(o)) die("copied a dead object");
                o->rc++;
            }
            break;
        }
        default: break;
    }
    return dst;
}

Obj *new_object() {
    Obj *o = new Obj();
    o->rc = 1;
    g_objects.insert(o);
    return o;
}

void release_object(Obj *o) {
    if(!g_objects.count(o)) die("released a dead object");
    if(--o->rc == 0) {
        for(auto &kv : o->props) clear_var(&kv.second);
        g_objects.erase(o);
        delete o;
    }
}

u16 key_of(const PA_Unistring &k) { return u16(k.fString, k.fLength); }

struct Handle { char *data; };

void engine(short selector, EngineBlock *eb) {
    switch(selector) {
        case EX_CREATE_UNISTRING: {
            const PA_Unichar *src = (const PA_Unichar *)eb->fHandle;
            size_t len = 0;
            while(src[len]) len++;
            eb->fUniString1 = new_string(src, len);
            break;
        }
        case EX_DISPOSE_UNISTRING:
            free_string(&eb->fUniString1);
            break;
        case EX_SET_UNISTRING: {
            const PA_Unichar *src = (const PA_Unichar *)eb->fHandle;
            size_t len = 0;
            while(src[len]) len++;
            free_string(&eb->fUniString1);
            eb->fUniString1 = new_string(src, len);
            break;
        }
        case EX_VARIABLE_TO_STRING: {
            PA_Variable *v = (PA_Variable *)eb->fHandle;
            if(v->fType == eVK_Unistring) eb->fUniString1 = v->uValue.fString; /* borrowed */
            break;
        }
        case EX_CREATE_PICTURE:
            eb->fPicture = new_picture();
            break;
        case EX_DISPOSE_PICTURE:
            free_picture(eb->fPicture);
            break;
        case EX_CLEAR_VARIABLE:
            clear_var((PA_Variable *)eb->fPtr1);
            break;
        case EX_SET_OBJ_VALUE: {
            Obj *o = (Obj *)eb->fPtr1;
            if(!g_objects.count(o)) die("set on a dead object");
            u16 k = key_of(eb->fUniString1);
            auto it = o->props.find(k);
            if(it != o->props.end()) clear_var(&it->second);
            o->props[k] = copy_var(*(PA_Variable *)eb->fPtr2);
            break;
        }
        case EX_GET_OBJ_VALUE: {
            g_get_calls++;
            Obj *o = (Obj *)eb->fPtr1;
            PA_Variable *out = (PA_Variable *)eb->fPtr2;
            if(g_fail_get) {
                memset(out, 0, sizeof(*out));
                out->fType = eVK_Unistring;
                out->uValue.fString.fString = kPoison;
                out->uValue.fString.fLength = 4;
                eb->fError = -1;
                break;
            }
            if(!g_objects.count(o)) die("get on a dead object");
            auto it = o->props.find(key_of(eb->fUniString1));
            if(it == o->props.end()) {
                memset(out, 0, sizeof(*out));
                out->fType = eVK_Undefined;
            } else {
                *out = copy_var(it->second);
            }
            break;
        }
        case EX_EXECUTE_COMMAND_BY_ID: {
            PA_Variable **params = (PA_Variable **)eb->fPtr1;
            PA_Variable *ret = (PA_Variable *)eb->fPtr2;
            if(eb->fParam1 == 1231 /* OB Is defined */) {
                Obj *o = (Obj *)params[0]->uValue.fObject;
                ret->fType = eVK_Boolean;
                ret->uValue.fBoolean = o && o->props.count(key_of(params[1]->uValue.fString)) ? 1 : 0;
            } else if(eb->fParam1 == 1225 /* OB Copy */) {
                Obj *src = (Obj *)params[0]->uValue.fObject;
                Obj *dst = new_object();
                for(auto &kv : src->props) dst->props[kv.first] = copy_var(kv.second);
                ret->fType = eVK_Object;
                ret->uValue.fObject = (PA_ObjectRef)dst;
            } else if(eb->fParam1 == 1217 /* JSON Stringify */) {
                if(g_fail_stringify) {
                    eb->fError = -1; /* result stays eVK_Undefined (set by the SDK) */
                } else {
                    static const PA_Unichar json[] = { '{', '}', 0 };
                    ret->fType = eVK_Unistring;
                    ret->uValue.fString = new_string(json, 2);
                }
            } else {
                die("unexpected PA_ExecuteCommandByID");
            }
            break;
        }
        case EX_RUN_IN_MAIN_PROCESS:
            ((PA_RunInMainProcessProcPtr)eb->fPtr1)(eb->fPtr2);
            break;
        case EX_HANDLE_MANAGER: {
            switch(eb->fParam1) {
                case 1: {
                    Handle *h = (Handle *)malloc(sizeof(Handle));
                    h->data = (char *)malloc(eb->fParam2 ? eb->fParam2 : 1);
                    g_handles.insert(h);
                    eb->fHandle = (PA_Handle)h;
                    break;
                }
                case 2: {
                    Handle *h = (Handle *)eb->fHandle;
                    if(!g_handles.erase(h)) die("disposed an unknown handle");
                    free(h->data);
                    free(h);
                    break;
                }
                case 5: eb->fParam3 = (sLONG_PTR)((Handle *)eb->fHandle)->data; break;
                case 6: break;
                default: die("unexpected handle manager op");
            }
            break;
        }
        default: {
            char msg[64];
            snprintf(msg, sizeof(msg), "unmodelled engine selector %d", selector);
            die(msg);
        }
    }
}

/* ---------- test helpers ---------- */

PA_Unistring U(const char *ascii) {
    u16 s;
    for(const char *p = ascii; *p; p++) s.push_back((PA_Unichar)*p);
    return new_string(s.c_str(), s.size());
}

void put(Obj *o, const char *key, PA_Variable v) {
    PA_Unistring k = U(key);
    PA_SetObjectProperty((PA_ObjectRef)o, &k, v);
    PA_DisposeUnistring(&k);
}

PA_Variable real_var(double d) { PA_Variable v; memset(&v, 0, sizeof(v)); PA_SetRealVariable(&v, d); return v; }
PA_Variable bool_var(bool b) { PA_Variable v; memset(&v, 0, sizeof(v)); PA_SetBooleanVariable(&v, b); return v; }
PA_Variable text_var(const char *s) { PA_Variable v; memset(&v, 0, sizeof(v)); PA_Unistring u = U(s); PA_SetStringVariable(&v, &u); return v; }

void put_text(Obj *o, const char *key, const char *value) {
    PA_Variable v = text_var(value);
    put(o, key, v);
    PA_ClearVariable(&v);
}

/* Stores a fresh collection under key; the parent holds the only reference. */
Obj *put_collection(Obj *o, const char *key) {
    Obj *c = new_object();
    PA_Variable v; memset(&v, 0, sizeof(v));
    PA_SetCollectionVariable(&v, (PA_CollectionRef)c);
    put(o, key, v);
    PA_ClearVariable(&v);
    return c;
}

bool check(bool ok, const char *what) {
    if(!ok) fprintf(stderr, "  check failed: %s\n", what);
    return ok;
}

bool no_leaks() {
    bool ok = true;
    if(!g_strings.empty()) { fprintf(stderr, "  LEAK: %zu engine string(s)\n", g_strings.size()); ok = false; }
    if(!g_pictures.empty()) { fprintf(stderr, "  LEAK: %zu engine picture(s)\n", g_pictures.size()); ok = false; }
    if(!g_objects.empty()) { fprintf(stderr, "  LEAK: %zu engine object(s)/collection(s)\n", g_objects.size()); ok = false; }
    if(!g_handles.empty()) { fprintf(stderr, "  LEAK: %zu engine handle(s)\n", g_handles.size()); ok = false; }
    return ok;
}

/* Runs a plugin command through the real PluginMain entry point with text
   parameters and a text result slot. */
u16 run_command(PA_long32 selector, const char *a, const char *b) {
    PA_Unistring p1 = U(a), p2 = U(b);
    void *params[2] = { &p1, &p2 };
    PA_Unistring result = U("");
    PA_Unistring *resultPtr = &result;
    PluginBlock block;
    block.fParameters = params;
    block.fResult = &resultPtr;
    block.fData = 0;
    PluginMain(selector, &block);
    u16 out(result.fString, result.fLength);
    PA_DisposeUnistring(&result);
    PA_DisposeUnistring(&p1);
    PA_DisposeUnistring(&p2);
    return out;
}

std::string narrow(const u16 &s) { std::string r; for(PA_Unichar c : s) r.push_back((char)c); return r; }

/* ---------- tests ---------- */

typedef std::function<bool()> Test;

std::map<std::string, Test> tests() {
    std::map<std::string, Test> t;

    /* Defects (dormant support/4DPlugin-JSON.cpp): object setters leak the
       empty owner allocated by PA_CreateVariable(eVK_Picture/eVK_Unistring)
       before the shallow set. */
    t["defect_ob_set_p_empty_owner"] = [] {
        Obj *o = new_object();
        ob_set_p((PA_ObjectRef)o, L"pic", (PA_Picture)new_picture()); /* helper takes the picture */
        bool ok = check(g_pictures.size() == 1, "only the object's copy of the picture is live");
        release_object(o);
        return no_leaks() && ok;
    };
    t["defect_ob_set_s_wkey_empty_owner"] = [] {
        Obj *o = new_object();
        ob_set_s((PA_ObjectRef)o, L"k", "value");
        release_object(o);
        return no_leaks();
    };
    t["defect_ob_set_s_ckey_empty_owner"] = [] {
        Obj *o = new_object();
        ob_set_s((PA_ObjectRef)o, "k", "value");
        release_object(o);
        return no_leaks();
    };
    t["defect_ob_set_a_wchar_empty_owner"] = [] {
        Obj *o = new_object();
        ob_set_a((PA_ObjectRef)o, L"k", L"value");
        release_object(o);
        return no_leaks();
    };
    t["defect_ob_set_a_u16_empty_owner"] = [] {
        Obj *o = new_object();
        CUTF16String v;
        for(const char *p = "value"; *p; p++) v.push_back((PA_Unichar)*p);
        ob_set_a((PA_ObjectRef)o, L"k", &v);
        release_object(o);
        return no_leaks();
    };

    /* Defects (dormant): PA_GetObjectProperty results are owned and never cleared. */
    t["defect_ob_get_a_result_not_cleared"] = [] {
        Obj *o = new_object();
        put_text(o, "k", "hello");
        CUTF8String out;
        bool ok = check(ob_get_a((PA_ObjectRef)o, L"k", &out), "defined");
        ok = check(out == CUTF8String((const uint8_t *)"hello"), "value") && ok;
        release_object(o);
        return no_leaks() && ok;
    };
    t["defect_ob_get_n_wrong_kind_not_cleared"] = [] {
        Obj *o = new_object();
        put_text(o, "n", "not a number");
        bool ok = check(ob_get_n((PA_ObjectRef)o, L"n") == 0.0, "wrong kind reads as 0");
        release_object(o);
        return no_leaks() && ok;
    };
    t["defect_ob_get_b_wrong_kind_not_cleared"] = [] {
        Obj *o = new_object();
        put_text(o, "flag", "true");
        bool ok = check(ob_get_b((PA_ObjectRef)o, L"flag") == false, "wrong kind reads as false");
        release_object(o);
        return no_leaks() && ok;
    };
    t["defect_ob_get_c_reference_not_released"] = [] {
        Obj *o = new_object();
        Obj *c = put_collection(o, "items");
        PA_CollectionRef got = ob_get_c((PA_ObjectRef)o, L"items");
        bool ok = check(got == (PA_CollectionRef)c, "same collection");
        ok = check(c->rc == 1, "result is borrowed from the parent") && ok;
        release_object(o);
        return no_leaks() && ok;
    };
    t["defect_ob_get_c_wrong_kind_not_cleared"] = [] {
        Obj *o = new_object();
        put_text(o, "items", "not a collection");
        bool ok = check(ob_get_c((PA_ObjectRef)o, L"items") == NULL, "wrong kind reads as NULL");
        release_object(o);
        return no_leaks() && ok;
    };
    /* Guard (contract hardening): on a failed PA_GetObjectProperty the
       uninitialised result is neither read nor cleared. */
    t["guard_get_error_result_untouched"] = [] {
        Obj *o = new_object();
        put_text(o, "k", "v");
        put(o, "n", real_var(1));
        put_collection(o, "c");
        g_fail_get = true;
        CUTF8String out;
        ob_get_a((PA_ObjectRef)o, L"k", &out);
        ob_get_n((PA_ObjectRef)o, L"n");
        ob_get_b((PA_ObjectRef)o, L"k");
        PA_CollectionRef c = ob_get_c((PA_ObjectRef)o, L"c");
        g_fail_get = false;
        bool ok = check(g_get_calls == 4, "getter reached the engine");
        ok = check(out.empty(), "failed result was not read") && ok;
        ok = check(c == NULL, "failed collection result was not read") && ok;
        release_object(o);
        return no_leaks() && ok;
    };

    /* Controls: must pass on both baseline and fixed sources. */
    t["control_ob_set_scalars"] = [] {
        Obj *o = new_object();
        ob_set_n((PA_ObjectRef)o, L"n", 1.5);
        ob_set_n((PA_ObjectRef)o, "n2", 2.5);
        ob_set_i((PA_ObjectRef)o, L"i", 7);
        ob_set_b((PA_ObjectRef)o, L"b", true);
        ob_set_0((PA_ObjectRef)o, L"z");
        ob_set_0((PA_ObjectRef)o, "z2");
        bool ok = check(o->props.size() == 6, "six properties");
        release_object(o);
        return no_leaks() && ok;
    };
    t["control_ob_set_o_c_transfer"] = [] {
        Obj *o = new_object();
        Obj *child = new_object();
        Obj *child2 = new_object();
        Obj *coll = new_object();
        ob_set_o((PA_ObjectRef)o, L"child", (PA_ObjectRef)child);
        ob_set_o((PA_ObjectRef)o, "child2", (PA_ObjectRef)child2);
        ob_set_c((PA_ObjectRef)o, L"coll", (PA_CollectionRef)coll);
        bool ok = check(child->rc == 1 && child2->rc == 1 && coll->rc == 1, "parent holds the only references");
        release_object(o);
        return no_leaks() && ok;
    };
    t["control_ob_get_scalars"] = [] {
        Obj *o = new_object();
        put(o, "x", real_var(2.25));
        put(o, "f", bool_var(true));
        bool ok = check(ob_is_defined((PA_ObjectRef)o, L"x"), "defined");
        ok = check(!ob_is_defined((PA_ObjectRef)o, L"y"), "undefined") && ok;
        ok = check(ob_get_n((PA_ObjectRef)o, L"x") == 2.25, "real value") && ok;
        ok = check(ob_get_b((PA_ObjectRef)o, L"f") == true, "bool value") && ok;
        ok = check(ob_get_c((PA_ObjectRef)o, L"missing") == NULL, "missing collection") && ok;
        release_object(o);
        return no_leaks() && ok;
    };
    t["control_ob_stringify"] = [] {
        Obj *o = new_object();
        put_text(o, "k", "v");
        CUTF8String json;
        ob_stringify((PA_ObjectRef)o, &json);
        bool ok = check(json == CUTF8String((const uint8_t *)"{}"), "stringified");
        g_fail_stringify = true;
        CUTF8String none;
        ob_stringify((PA_ObjectRef)o, &none);
        g_fail_stringify = false;
        ok = check(none.empty(), "failed stringify leaves output empty") && ok;
        release_object(o);
        return no_leaks() && ok;
    };

    /* Controls: the shipped commands through the real PluginMain. */
    t["control_cmd_put_get_environment_variable"] = [] {
        run_command(3, "ENV_OWNERSHIP_TEST", "hello");
        bool ok = check(narrow(run_command(2, "ENV_OWNERSHIP_TEST", "")) == "hello", "round trip");
        ok = check(narrow(run_command(2, "ENV_OWNERSHIP_UNSET_X", "")).empty(), "unset reads empty") && ok;
        ok = check(narrow(run_command(2, "", "")).empty(), "empty name") && ok;
        run_command(3, "BAD=NAME", "x");
        ok = check(getenv("BAD") == NULL, "name with '=' rejected") && ok;
        return no_leaks() && ok;
    };
    t["control_cmd_expand_and_register"] = [] {
        bool ok = check(run_command(1, "%PATH%", "").empty(), "expand is Windows-only");
        PA_long32 ref = 0;
        void *params[1] = { &ref };
        PluginBlock block; block.fParameters = params; block.fResult = 0; block.fData = 0;
        PluginMain(4, &block);
        return no_leaks() && ok;
    };
    t["control_init_deinit"] = [] {
        PluginBlock block; block.fParameters = 0; block.fResult = 0; block.fData = 0;
        PluginMain(kInitPlugin, &block);
        PluginMain(kDeinitPlugin, &block);
        return no_leaks();
    };
    return t;
}

} // namespace

int main(int argc, char **argv) {
    gCall4D = engine;
    std::map<std::string, Test> all = tests();
    if(argc == 2 && !strcmp(argv[1], "--list")) {
        for(auto &kv : all) printf("%s\n", kv.first.c_str());
        return 0;
    }
    if(argc != 2 || !all.count(argv[1])) {
        fprintf(stderr, "usage: %s --list | <test>\n", argv[0]);
        return 2;
    }
    return all[argv[1]]() ? 0 : 1;
}
