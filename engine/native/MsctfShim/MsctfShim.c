/*
 * Native msctf.dll proxy for Wine.
 *
 * The goal is not to reimplement TSF. The goal is to intercept the Wine
 * msctf gaps that ATOK is currently hitting, while forwarding the rest to
 * the Wine-built msctf.dll that ships on disk.
 */

#include <windows.h>
#include <objbase.h>
#include <oleauto.h>
#include <msctf.h>
#include <textstor.h>

#pragma comment(linker, "/export:DllGetClassObject=DllGetClassObject@12")
#pragma comment(linker, "/export:DllCanUnloadNow=DllCanUnloadNow@0")

static const CLSID CLSID_ATOK_TIP = {
    0x1314EB53, 0xCACA, 0x4152, {0xA5, 0x56, 0xA1, 0x84, 0x14, 0x32, 0x02, 0xAF}
};
static const GUID GUID_ATOK_PROFILE = {
    0xa38f2fd9, 0x7199, 0x45e1, {0x84, 0x1c, 0xbe, 0x03, 0x13, 0xd8, 0x05, 0x2f}
};
static const GUID GUID_TFCAT_TIP_KEYBOARD_LOCAL = {
    0x34745c63, 0xb2f0, 0x4784, {0x8b, 0x67, 0x5e, 0x12, 0xc8, 0x70, 0x1a, 0x31}
};
#define DEFINE_LOCAL_IID(name, l1, w1, w2, b1, b2, b3, b4, b5, b6, b7, b8) \
    static const GUID name = { l1, w1, w2, { b1, b2, b3, b4, b5, b6, b7, b8 } }
DEFINE_LOCAL_IID(IID_LOCAL_ITfKeyEventSink, 0xaa80e7f5, 0x2021, 0x11d2, 0x93, 0xe0, 0x00, 0x60, 0xb0, 0x67, 0xb8, 0x6e);
DEFINE_LOCAL_IID(IID_LOCAL_ITfTextEditSink, 0x8127d409, 0xccd3, 0x4683, 0x96, 0x7a, 0xb4, 0x3d, 0x5b, 0x48, 0x2b, 0xf7);
DEFINE_LOCAL_IID(IID_LOCAL_ITfTextInputProcessor, 0xaa80e7f7, 0x2021, 0x11d2, 0x93, 0xe0, 0x00, 0x60, 0xb0, 0x67, 0xb8, 0x6e);
DEFINE_LOCAL_IID(IID_LOCAL_ITfEditSession, 0xaa80e803, 0x2021, 0x11d2, 0x93, 0xe0, 0x00, 0x60, 0xb0, 0x67, 0xb8, 0x6e);
DEFINE_LOCAL_IID(IID_LOCAL_ITfEditRecord, 0x42d4d099, 0x7c1a, 0x4a89, 0xb8, 0x36, 0x6c, 0x6f, 0x22, 0x16, 0x0d, 0xf0);
DEFINE_LOCAL_IID(IID_LOCAL_ITfThreadMgrEventSink, 0xaa80e80e, 0x2021, 0x11d2, 0x93, 0xe0, 0x00, 0x60, 0xb0, 0x67, 0xb8, 0x6e);
DEFINE_LOCAL_IID(IID_LOCAL_ITfThreadMgrEx, 0x3e90ade3, 0x7594, 0x4cb0, 0xbb, 0x58, 0x69, 0x62, 0x8f, 0x5f, 0x45, 0x8c);
DEFINE_LOCAL_IID(IID_LOCAL_ITfSourceSingle, 0x73131f9c, 0x56a9, 0x49dd, 0xb0, 0xee, 0xd0, 0x46, 0x63, 0x3f, 0x75, 0x28);
DEFINE_LOCAL_IID(IID_LOCAL_ITfCompartment, 0xbb08f7a9, 0x607a, 0x4384, 0x86, 0x23, 0x05, 0x68, 0x92, 0xb6, 0x43, 0x71);
DEFINE_LOCAL_IID(IID_LOCAL_ITfCompartmentEventSink, 0x743abd5f, 0xf26d, 0x48df, 0x8c, 0xc5, 0x23, 0x84, 0x92, 0x41, 0x9b, 0x64);
DEFINE_LOCAL_IID(IID_LOCAL_ITfCompartmentMgr, 0x7dcf57ac, 0x18ad, 0x438b, 0x82, 0x4d, 0x97, 0x9b, 0xff, 0xb7, 0x4b, 0x7c);
DEFINE_LOCAL_IID(IID_LOCAL_ITfMessagePump, 0x8f1b8ad8, 0x0b6b, 0x4874, 0x90, 0xc5, 0xbd, 0x76, 0x01, 0x1e, 0x8f, 0x7c);
DEFINE_LOCAL_IID(IID_LOCAL_ITfLangBarItemMgr, 0xba468c55, 0x9956, 0x4fb1, 0xa5, 0x9d, 0x52, 0xa7, 0xdd, 0x7c, 0xc6, 0xaa);
DEFINE_LOCAL_IID(IID_LOCAL_IEnumTfLangBarItems, 0x583f34d0, 0xde25, 0x11d2, 0xaf, 0xdd, 0x00, 0x10, 0x5a, 0x27, 0x99, 0xb5);
DEFINE_LOCAL_IID(IID_LOCAL_ITfUIElementMgr, 0xea1ea135, 0x19df, 0x11d7, 0xa6, 0xd2, 0x00, 0x06, 0x5b, 0x84, 0x43, 0x5c);
DEFINE_LOCAL_IID(IID_LOCAL_ITfCategoryMgr, 0xc3acefb5, 0xf69d, 0x4905, 0x93, 0x8f, 0xfc, 0xad, 0xcf, 0x4b, 0xe8, 0x30);
DEFINE_LOCAL_IID(IID_LOCAL_IClassFactory, 0x00000001, 0x0000, 0x0000, 0xc0, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x46);
DEFINE_LOCAL_IID(IID_LOCAL_ITfDisplayAttributeMgr, 0x8ded7393, 0x5db1, 0x475c, 0x9e, 0x71, 0xa3, 0x91, 0x11, 0xb0, 0xff, 0x67);
DEFINE_LOCAL_IID(IID_LOCAL_ITfContextOwnerServices, 0xb23eb630, 0x3e1c, 0x11d3, 0xa7, 0x45, 0x00, 0x50, 0x04, 0x0a, 0xb4, 0x07);
DEFINE_LOCAL_IID(IID_LOCAL_ITfInsertAtSelection, 0x55ce16ba, 0x3014, 0x41c1, 0x9c, 0xeb, 0xfa, 0xde, 0x14, 0x46, 0xac, 0x6c);
DEFINE_LOCAL_IID(IID_LOCAL_ITextStoreACP, 0x28888fe3, 0xc2a0, 0x483a, 0xa3, 0xea, 0x8c, 0xb1, 0xce, 0x51, 0xff, 0x3d);
DEFINE_LOCAL_IID(IID_LOCAL_ITfFunctionProvider, 0x101d6610, 0x0990, 0x11d3, 0x8d, 0xf0, 0x00, 0x10, 0x5a, 0x27, 0x99, 0xb5);
DEFINE_LOCAL_IID(IID_LOCAL_IEnumTfFunctionProviders, 0xe4b24db0, 0x0990, 0x11d3, 0x8d, 0xf0, 0x00, 0x10, 0x5a, 0x27, 0x99, 0xb5);
DEFINE_LOCAL_IID(IID_LOCAL_ITfThreadFocusSink, 0xc0f1db0c, 0x3a20, 0x405c, 0xa3, 0x03, 0x96, 0xb6, 0x01, 0x0a, 0x88, 0x5f);
DEFINE_LOCAL_IID(IID_LOCAL_ITfActiveLangProfileNotifySink, 0xb246cb75, 0xa93e, 0x4652, 0xbf, 0x8c, 0xb3, 0xfe, 0x0c, 0xfd, 0x7e, 0x57);
DEFINE_LOCAL_IID(GUID_LOCAL_COMPARTMENT_KEYBOARD_OPENCLOSE, 0x58273aad, 0x01bb, 0x4164, 0x95, 0xc6, 0x75, 0x5b, 0xa0, 0xb5, 0x16, 0x2d);
DEFINE_LOCAL_IID(GUID_LOCAL_COMPARTMENT_KEYBOARD_DISABLED, 0x71a5b253, 0x1951, 0x466b, 0x9f, 0xbc, 0x9c, 0x88, 0x08, 0xfa, 0x84, 0xf2);
DEFINE_LOCAL_IID(GUID_LOCAL_COMPARTMENT_EMPTYCONTEXT, 0xd7487dbf, 0x804e, 0x41c5, 0x89, 0x4d, 0xad, 0x96, 0xfd, 0x4e, 0xea, 0x13);
DEFINE_LOCAL_IID(IID_LOCAL_ITfCandidateListUIElement, 0xd248b1ab, 0x967a, 0x4dc6, 0xa2, 0x90, 0x8f, 0x90, 0xa3, 0x7e, 0x9e, 0x08);

/*
 * Local ITfCandidateListUIElement view (Wine's msctf.h may not declare it).
 * During henkan, ATOK hands its candidate window to
 * ITfUIElementMgr::BeginUIElement as an ITfUIElement; QI'ing that element to
 * this interface lets us read the candidate list (the top-N alternatives modore
 * needs for candidate cycling). Layout = IUnknown + ITfUIElement + the
 * candidate-list methods, in vtable order.
 */
typedef struct ITfCandidateListUIElementLocal ITfCandidateListUIElementLocal;
typedef struct ITfCandidateListUIElementLocalVtbl {
    HRESULT (WINAPI *QueryInterface)(ITfCandidateListUIElementLocal *, REFIID, void **);
    ULONG (WINAPI *AddRef)(ITfCandidateListUIElementLocal *);
    ULONG (WINAPI *Release)(ITfCandidateListUIElementLocal *);
    /* ITfUIElement */
    HRESULT (WINAPI *GetDescription)(ITfCandidateListUIElementLocal *, BSTR *);
    HRESULT (WINAPI *GetGUID)(ITfCandidateListUIElementLocal *, GUID *);
    HRESULT (WINAPI *Show)(ITfCandidateListUIElementLocal *, BOOL);
    HRESULT (WINAPI *IsShown)(ITfCandidateListUIElementLocal *, BOOL *);
    /* ITfCandidateListUIElement */
    HRESULT (WINAPI *GetUpdatedFlags)(ITfCandidateListUIElementLocal *, DWORD *);
    HRESULT (WINAPI *GetDocumentMgr)(ITfCandidateListUIElementLocal *, void **);
    HRESULT (WINAPI *GetCount)(ITfCandidateListUIElementLocal *, UINT *);
    HRESULT (WINAPI *GetSelection)(ITfCandidateListUIElementLocal *, UINT *);
    HRESULT (WINAPI *GetString)(ITfCandidateListUIElementLocal *, UINT, BSTR *);
    HRESULT (WINAPI *GetPageIndex)(ITfCandidateListUIElementLocal *, UINT *, UINT, UINT *);
    HRESULT (WINAPI *GetCurrentPage)(ITfCandidateListUIElementLocal *, UINT *);
} ITfCandidateListUIElementLocalVtbl;
struct ITfCandidateListUIElementLocal { const ITfCandidateListUIElementLocalVtbl *lpVtbl; };

/* minimal vtables so we can deliver the activation callbacks ATOK waits for */
typedef struct ITfThreadFocusSinkLocalVtbl {
    void *QueryInterface; void *AddRef; void *Release;
    HRESULT (WINAPI *OnSetThreadFocus)(void *);
    HRESULT (WINAPI *OnKillThreadFocus)(void *);
} ITfThreadFocusSinkLocalVtbl;
typedef struct ITfThreadFocusSinkLocal { ITfThreadFocusSinkLocalVtbl *lpVtbl; } ITfThreadFocusSinkLocal;
typedef struct ITfActiveLangSinkLocalVtbl {
    void *QueryInterface; void *AddRef; void *Release;
    HRESULT (WINAPI *OnActivated)(void *, REFCLSID, REFGUID, BOOL);
} ITfActiveLangSinkLocalVtbl;
typedef struct ITfActiveLangSinkLocal { ITfActiveLangSinkLocalVtbl *lpVtbl; } ITfActiveLangSinkLocal;
typedef struct ITfInsertAtSelectionLocal ITfInsertAtSelectionLocal;
typedef struct ITfInsertAtSelectionLocalVtbl {
    HRESULT (WINAPI *QueryInterface)(ITfInsertAtSelectionLocal *, REFIID, void **);
    ULONG (WINAPI *AddRef)(ITfInsertAtSelectionLocal *);
    ULONG (WINAPI *Release)(ITfInsertAtSelectionLocal *);
    HRESULT (WINAPI *InsertTextAtSelection)(ITfInsertAtSelectionLocal *, TfEditCookie, DWORD, const WCHAR *, LONG, ITfRange **);
    HRESULT (WINAPI *InsertEmbeddedAtSelection)(ITfInsertAtSelectionLocal *, TfEditCookie, DWORD, IDataObject *, ITfRange **);
} ITfInsertAtSelectionLocalVtbl;
struct ITfInsertAtSelectionLocal {
    const ITfInsertAtSelectionLocalVtbl *lpVtbl;
};
#ifndef TF_IAS_NOQUERY
#define TF_IAS_NOQUERY 0x1
#endif
#ifndef TF_IAS_QUERYONLY
#define TF_IAS_QUERYONLY 0x2
#endif
#ifndef TF_IAS_NO_DEFAULT_COMPOSITION
#define TF_IAS_NO_DEFAULT_COMPOSITION 0x80000000
#endif
static const CLSID CLSID_LOCAL_TF_CategoryMgr = { 0xa4b544a1, 0x438d, 0x4b41, { 0x93, 0x25, 0x86, 0x95, 0x23, 0xe2, 0xd6, 0xc7 } };
#define LANGID_JA 0x0411

typedef HRESULT (WINAPI *PFN_TF_CreateThreadMgr)(ITfThreadMgr **);
typedef HRESULT (WINAPI *PFN_TF_CreateInputProcessorProfiles)(ITfInputProcessorProfiles **);
typedef HRESULT (WINAPI *PFN_TF_CreateLangBarItemMgr)(ITfLangBarItemMgr **);
typedef HRESULT (WINAPI *PFN_DllGetClassObject)(REFCLSID, REFIID, void **);
typedef HRESULT (WINAPI *PFN_DllCanUnloadNow)(void);
typedef BSTR (WINAPI *PFN_SysAllocString)(const OLECHAR *);
typedef void (WINAPI *PFN_SysFreeString)(BSTR);
typedef HRESULT (WINAPI *PFN_CoRegisterClassObject)(REFCLSID, LPUNKNOWN, DWORD, DWORD, LPDWORD);
typedef HRESULT (WINAPI *PFN_CoCreateInstanceLocal)(REFCLSID, LPUNKNOWN, DWORD, REFIID, LPVOID *);
typedef void (WINAPI *PFN_AtNsShimPatchNow)(void);

typedef struct MsctfProfileEnum {
    IEnumTfLanguageProfiles iface;
    LONG refs;
    ULONG index;
    TF_LANGUAGEPROFILE profile;
} MsctfProfileEnum;

typedef struct MsctfContextWrap MsctfContextWrap;

typedef struct MsctfProfilesWrap {
    ITfInputProcessorProfiles iface;
    LONG refs;
    ITfInputProcessorProfiles *orig;
    LANGID current_lang;
    BOOL active;
    TF_LANGUAGEPROFILE active_profile;
} MsctfProfilesWrap;

typedef enum MsctfSourceOwnerKind {
    SOURCE_OWNER_THREADMGR,
    SOURCE_OWNER_CONTEXT
} MsctfSourceOwnerKind;

#define MSCTF_SOURCE_SINK_MAX 16

typedef struct SourceSinkEntry {
    IUnknown *sink;
    IID sink_iid;
    DWORD cookie;
} SourceSinkEntry;

typedef struct MsctfSourceWrap {
    ITfSource iface;
    LONG refs;
    IUnknown *sink;
    IID sink_iid;
    DWORD cookie;
    DWORD next_cookie;
    SourceSinkEntry sinks[MSCTF_SOURCE_SINK_MAX];
    ULONG sink_count;
    void *owner;
    MsctfSourceOwnerKind owner_kind;
} MsctfSourceWrap;

typedef struct MsctfKeystrokeWrap {
    ITfKeystrokeMgr iface;
    LONG refs;
    ITfKeyEventSink *sink;
    TfClientId tid;
    BOOL foreground;
    CLSID clsid;
} MsctfKeystrokeWrap;

typedef struct MsctfSourceSingleWrap {
    ITfSourceSingle iface;
    LONG refs;
    IUnknown *sink;
    GUID sink_iid;
    TfClientId tid;
    void *owner;
} MsctfSourceSingleWrap;

typedef struct MsctfCompartmentMgrWrap {
    ITfCompartmentMgr iface;
    LONG refs;
    ITfCompartmentMgr *orig;
    void *owner;
} MsctfCompartmentMgrWrap;

typedef struct MsctfCompartmentWrap {
    ITfCompartment iface;
    ITfSource source_iface;
    LONG refs;
    ITfCompartment *orig;
    GUID guid;
    IUnknown *sink;
    IID sink_iid;
    DWORD cookie;
    DWORD next_cookie;
} MsctfCompartmentWrap;

typedef struct MsctfUIElementMgrWrap {
    ITfUIElementMgr iface;
    LONG refs;
    ITfUIElementMgr *orig;
    ITfUIElement *cached_element;
    DWORD cached_id;
} MsctfUIElementMgrWrap;

#define MSCTF_LANG_BAR_ITEM_MAX 64
#define MSCTF_LANG_BAR_SINK_MAX 16

typedef struct MsctfLangBarItemMgrWrap MsctfLangBarItemMgrWrap;

typedef struct LangBarStoredItem {
    ITfLangBarItem *item;
    GUID guid;
} LangBarStoredItem;

typedef struct LangBarSinkEntry {
    ITfLangBarItemSink *sink;
    GUID guid;
    DWORD cookie;
} LangBarSinkEntry;

typedef struct MsctfLangBarItemEnum {
    IEnumTfLangBarItems iface;
    LONG refs;
    MsctfLangBarItemMgrWrap *mgr;
    ULONG index;
} MsctfLangBarItemEnum;

typedef struct MsctfLangBarItemMgrWrap {
    ITfLangBarItemMgr iface;
    LONG refs;
    ITfLangBarItemMgr *orig;
    void *owner;
    ULONG item_count;
    LangBarStoredItem items[MSCTF_LANG_BAR_ITEM_MAX];
    LangBarSinkEntry sinks[MSCTF_LANG_BAR_SINK_MAX];
    ULONG sink_count;
    DWORD next_sink_cookie;
} MsctfLangBarItemMgrWrap;

typedef struct MsctfCategoryMgrWrap {
    ITfCategoryMgr iface;
    LONG refs;
} MsctfCategoryMgrWrap;

#define MSCTF_GUID_ATOM_MAX 256

typedef struct MsctfGuidAtomEntry {
    GUID guid;
    TfGuidAtom atom;
} MsctfGuidAtomEntry;

#define MSCTF_COMPARTMENT_VALUE_MAX 64

typedef struct MsctfCompartmentValueEntry {
    GUID guid;
    VARIANT value;
    BOOL valid;
} MsctfCompartmentValueEntry;

typedef struct MsctfFunctionProviderWrap MsctfFunctionProviderWrap;
typedef struct MsctfFunctionProviderEnumWrap MsctfFunctionProviderEnumWrap;
typedef struct MsctfEditRecordWrap MsctfEditRecordWrap;

typedef struct MsctfThreadMgrWrap {
    ITfThreadMgr iface;
    LONG refs;
    ITfThreadMgr *orig;
    MsctfSourceWrap *source;
    MsctfSourceSingleWrap *source_single;
    MsctfKeystrokeWrap *km;
    MsctfFunctionProviderWrap *provider;
    ITfFunctionProvider *advised_provider;
    MsctfFunctionProviderEnumWrap *provider_enum;
    struct MsctfThreadMgrExWrap *ex;
    struct MsctfDocMgrWrap *docmgr;
    TfClientId client_id;
    BOOL active;
} MsctfThreadMgrWrap;

typedef struct MsctfThreadMgrExWrap {
    ITfThreadMgrEx iface;
    LONG refs;
    MsctfThreadMgrWrap *tm;
} MsctfThreadMgrExWrap;

static MsctfThreadMgrExWrap *g_last_tmex_wrap;

typedef struct MsctfDocMgrWrap MsctfDocMgrWrap;
typedef struct MsctfRangeWrap MsctfRangeWrap;
typedef struct MsctfCompositionViewWrap MsctfCompositionViewWrap;
typedef struct MsctfCompositionWrap MsctfCompositionWrap;
typedef struct MsctfContextCompositionWrap MsctfContextCompositionWrap;
typedef struct MsctfContextEnumWrap MsctfContextEnumWrap;
typedef struct MsctfCompositionEnumWrap MsctfCompositionEnumWrap;
typedef struct MsctfEditSessionWrap MsctfEditSessionWrap;

struct MsctfFunctionProviderWrap {
    ITfFunctionProvider iface;
    LONG refs;
    MsctfThreadMgrWrap *tm;
    CLSID clsid;
    GUID type_guid;
};

struct MsctfFunctionProviderEnumWrap {
    IEnumTfFunctionProviders iface;
    LONG refs;
    MsctfThreadMgrWrap *tm;
    BOOL returned;
    MsctfFunctionProviderWrap *provider;
};

struct MsctfRangeWrap {
    ITfRange iface;
    LONG refs;
    MsctfContextWrap *ctx;
    LONG start;
    LONG end;
};

struct MsctfCompositionViewWrap {
    ITfCompositionView iface;
    LONG refs;
    MsctfContextWrap *ctx;
    MsctfRangeWrap *range;
};

struct MsctfCompositionWrap {
    ITfComposition iface;
    LONG refs;
    MsctfContextWrap *ctx;
    MsctfRangeWrap *range;
    ITfCompositionSink *sink;
};

struct MsctfContextWrap {
    ITfContext iface;
    ITfContextComposition comp_iface;
    ITfInsertAtSelectionLocal insert_iface;
    LONG refs;
    ITfContext *orig;
    ITextStoreACP *textstore;
    MsctfDocMgrWrap *owner;
    MsctfSourceWrap *source;
    TfEditCookie edit_cookie;
    BOOL comp_active;
    WCHAR comp_text[256];
    ULONG comp_len;
    MsctfCompositionViewWrap *view;
    MsctfRangeWrap *range;
    MsctfCompositionWrap *composition;
};

struct MsctfContextCompositionWrap {
    ITfContextComposition iface;
    LONG refs;
    MsctfContextWrap *ctx;
};

struct MsctfContextEnumWrap {
    IEnumTfContexts iface;
    LONG refs;
    MsctfContextWrap *ctx;
    BOOL returned;
    IEnumTfContexts *orig;
};

struct MsctfCompositionEnumWrap {
    IEnumITfCompositionView iface;
    LONG refs;
    MsctfContextWrap *ctx;
    BOOL returned;
    IEnumITfCompositionView *orig;
};

struct MsctfEditRecordWrap {
    ITfEditRecord iface;
    LONG refs;
};

struct MsctfEditSessionWrap {
    ITfEditSession iface;
    LONG refs;
    ITfEditSession *orig;
    TfClientId tid;
    DWORD flags;
};

struct MsctfDocMgrWrap {
    ITfDocumentMgr iface;
    LONG refs;
    ITfDocumentMgr *orig;
    MsctfContextWrap *top;
};

static HMODULE g_helper;
static PFN_TF_CreateThreadMgr pTF_CreateThreadMgr;
static PFN_TF_CreateInputProcessorProfiles pTF_CreateInputProcessorProfiles;
static PFN_TF_CreateLangBarItemMgr pTF_CreateLangBarItemMgr;
static PFN_DllGetClassObject pDllGetClassObject;
static PFN_DllCanUnloadNow pDllCanUnloadNow;
static PFN_SysAllocString pSysAllocString;
static PFN_SysFreeString pSysFreeString;
static PFN_CoRegisterClassObject pCoRegisterClassObject;
static PFN_CoCreateInstanceLocal pCoCreateInstanceLocal;
static DWORD g_category_cookie;
static BOOL g_category_registered;
static IClassFactory *g_category_factory;
static MsctfGuidAtomEntry g_guid_atoms[MSCTF_GUID_ATOM_MAX];
static ULONG g_guid_atom_count;
static TfGuidAtom g_next_guid_atom = 1;
static MsctfCompartmentValueEntry g_compartment_values[MSCTF_COMPARTMENT_VALUE_MAX];
static ULONG g_compartment_value_count;
static MsctfContextWrap *g_active_context;
/* Last key event sink ATOK advised, regardless of which km wrapper instance it
 * used. The harness may hold a different km wrapper than the one ATOK advised
 * on, so we route keystrokes to this global sink as a fallback so ATOK actually
 * sees TestKeyDown/KeyDown. */
static ITfKeyEventSink *g_key_sink;
static TfClientId g_key_tid;
static BOOL g_profile_atok_pending;
static ITfTextInputProcessor *g_profile_tip;
static MsctfThreadMgrWrap *g_active_tm;
/* Singleton dummy lang-bar mgr: ATOK adds items then re-acquires the mgr and
 * expects the same instance/state. Returning a fresh per-call dummy made ATOK
 * see an inconsistent (empty) lang bar and roll back Activate. */
static MsctfLangBarItemMgrWrap *g_dummy_langbar;
static HANDLE g_log_file = INVALID_HANDLE_VALUE;
static HINSTANCE g_instance;
static WCHAR g_helper_path[MAX_PATH];

/* AT_QUIET: suppress the per-callback shim logging. ATOK makes many TSF range/
 * context callbacks per convert; each used to do two WriteFiles (stdout pipe +
 * file), adding syscall latency (and stdout-pipe backpressure in daemon mode) to
 * the QueryRange/GetReconversion hot path. The relay sets AT_QUIET=1. Checked once. */
static int g_quiet = -1;
static int log_quiet(void)
{
    if (g_quiet < 0) {
        WCHAR b[8];
        DWORD n = GetEnvironmentVariableW(L"AT_QUIET", b, 8);
        g_quiet = (n > 0 && b[0] && b[0] != L'0') ? 1 : 0;
    }
    return g_quiet;
}

static void log_init(void)
{
    if (g_log_file == INVALID_HANDLE_VALUE) {
        g_log_file = CreateFileA("tipruntime.log", FILE_APPEND_DATA,
                                 FILE_SHARE_READ | FILE_SHARE_WRITE,
                                 NULL, OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
    }
}

static void log_bytes(const char *s, DWORD len)
{
    DWORD w;
    if (log_quiet()) return;
    log_init();
    HANDLE h = GetStdHandle(STD_OUTPUT_HANDLE);
    if (h) WriteFile(h, s, len, &w, 0);
    if (g_log_file != INVALID_HANDLE_VALUE) WriteFile(g_log_file, s, len, &w, 0);
}

static void log_line(const char *s)
{
    log_bytes(s, (DWORD)lstrlenA(s));
}

static void log_hr(const char *tag, HRESULT hr)
{
    char buf[96];
    wsprintfA(buf, "%s hr=0x%08lX\r\n", tag, (unsigned long)hr);
    log_line(buf);
}

static void log_ptr_hr(const char *tag, const void *ptr, HRESULT hr)
{
    char buf[160];
    wsprintfA(buf, "%s ptr=%p hr=0x%08lX\r\n", tag, ptr, (unsigned long)hr);
    log_line(buf);
}

static void log_ptr2_hr(const char *tag, const void *a, const void *b, HRESULT hr)
{
    char buf[192];
    wsprintfA(buf, "%s a=%p b=%p hr=0x%08lX\r\n", tag, a, b, (unsigned long)hr);
    log_line(buf);
}

static BOOL env_flag_enabled(const WCHAR *name)
{
    WCHAR buf[8];
    return GetEnvironmentVariableW(name, buf, (DWORD)(sizeof(buf) / sizeof(buf[0]))) > 0 &&
           buf[0] != L'0';
}

static DWORD get_env_dword(const WCHAR *name, DWORD fallback)
{
    WCHAR buf[32];
    DWORD n = GetEnvironmentVariableW(name, buf, (DWORD)(sizeof(buf) / sizeof(buf[0])));
    DWORD v = 0;
    const WCHAR *p = buf;
    int hex = 0, any = 0;
    if (n == 0 || n >= 32) return fallback;
    if (p[0] == L'0' && (p[1] == L'x' || p[1] == L'X')) { hex = 1; p += 2; }
    for (; *p; p++) {
        WCHAR c = *p;
        if (c >= L'0' && c <= L'9') v = v * (hex ? 16 : 10) + (DWORD)(c - L'0');
        else if (hex && c >= L'a' && c <= L'f') v = v * 16 + (DWORD)(c - L'a' + 10);
        else if (hex && c >= L'A' && c <= L'F') v = v * 16 + (DWORD)(c - L'A' + 10);
        else break;
        any = 1;
    }
    return any ? v : fallback;
}

static void sync_context_ranges(MsctfContextWrap *c)
{
    if (!c) return;
    if (c->range) {
        c->range->start = 0;
        c->range->end = (LONG)c->comp_len;
    }
    if (c->view && c->view->range) {
        c->view->range->start = 0;
        c->view->range->end = (LONG)c->comp_len;
    }
    if (c->composition && c->composition->range) {
        c->composition->range->start = 0;
        c->composition->range->end = (LONG)c->comp_len;
    }
}

static void context_replace_comp_text(MsctfContextWrap *c, const WCHAR *text, LONG cch)
{
    ULONG max_chars;
    ULONG n;

    if (!c) return;
    max_chars = (ULONG)(sizeof(c->comp_text) / sizeof(c->comp_text[0])) - 1;
    if (!text || cch <= 0) {
        c->comp_len = 0;
        c->comp_text[0] = 0;
        sync_context_ranges(c);
        return;
    }
    n = (ULONG)cch;
    if (n > max_chars) n = max_chars;
    CopyMemory(c->comp_text, text, n * sizeof(WCHAR));
    c->comp_len = n;
    c->comp_text[n] = 0;
    sync_context_ranges(c);
}

static void sync_active_context_text(WCHAR ch)
{
    MsctfContextWrap *c = g_active_context;

    if (!c || !ch || !c->range) return;
    if (c->comp_len >= (sizeof(c->comp_text) / sizeof(c->comp_text[0])) - 1) return;

    c->comp_text[c->comp_len++] = ch;
    c->comp_text[c->comp_len] = 0;
    sync_context_ranges(c);
}

static const char *guid_string(REFGUID guid)
{
    static char buf[4][64];
    static int idx;
    char *out = buf[idx++ & 3];
    wsprintfA(out, "%08lX-%04X-%04X-%02X%02X-%02X%02X%02X%02X%02X%02X",
              (unsigned long)guid->Data1, guid->Data2, guid->Data3,
              guid->Data4[0], guid->Data4[1], guid->Data4[2], guid->Data4[3],
              guid->Data4[4], guid->Data4[5], guid->Data4[6], guid->Data4[7]);
    return out;
}

static void log_wchars_preview(const char *tag, const WCHAR *text, LONG cch)
{
    char buf[256];
    char *p = buf;
    LONG i, n;
    if (!tag) tag = "wstr";
    if (!text) {
        wsprintfA(buf, "%s text=NULL cch=%ld\r\n", tag, cch);
        log_line(buf);
        return;
    }
    if (cch < 0) cch = (LONG)lstrlenW(text);
    n = cch;
    if (n > 8) n = 8;
    p += wsprintfA(p, "%s cch=%ld", tag, cch);
    for (i = 0; i < n; i++) {
        p += wsprintfA(p, " U+%04X", text[i]);
    }
    if (n < cch) p += wsprintfA(p, " ...");
    *p++ = '\r';
    *p++ = '\n';
    *p = 0;
    log_line(buf);
}

static const char *iid_name(REFIID riid)
{
    if (IsEqualIID(riid, &IID_LOCAL_ITfKeyEventSink)) return "ITfKeyEventSink";
    if (IsEqualIID(riid, &IID_ITfContextOwnerCompositionSink)) return "ITfContextOwnerCompositionSink";
    if (IsEqualIID(riid, &IID_ITfContextComposition)) return "ITfContextComposition";
    if (IsEqualIID(riid, &IID_LOCAL_ITfCompartmentMgr)) return "ITfCompartmentMgr";
    if (IsEqualIID(riid, &IID_LOCAL_ITfMessagePump)) return "ITfMessagePump";
    if (IsEqualIID(riid, &IID_LOCAL_ITfLangBarItemMgr)) return "ITfLangBarItemMgr";
    if (IsEqualIID(riid, &IID_LOCAL_ITfUIElementMgr)) return "ITfUIElementMgr";
    if (IsEqualIID(riid, &IID_LOCAL_ITfCategoryMgr)) return "ITfCategoryMgr";
    if (IsEqualIID(riid, &IID_LOCAL_ITfDisplayAttributeMgr)) return "ITfDisplayAttributeMgr";
    if (IsEqualIID(riid, &IID_LOCAL_ITfContextOwnerServices)) return "ITfContextOwnerServices";
    if (IsEqualIID(riid, &IID_LOCAL_ITfThreadMgrEventSink)) return "ITfThreadMgrEventSink";
    if (IsEqualIID(riid, &IID_LOCAL_ITfTextEditSink)) return "ITfTextEditSink";
    if (IsEqualIID(riid, &IID_LOCAL_ITfInsertAtSelection)) return "ITfInsertAtSelection";
    if (IsEqualIID(riid, &IID_LOCAL_ITextStoreACP)) return "ITextStoreACP";
    if (IsEqualIID(riid, &IID_ITfSource)) return "ITfSource";
    if (IsEqualIID(riid, &IID_LOCAL_ITfThreadMgrEx)) return "ITfThreadMgrEx";
    if (IsEqualIID(riid, &IID_LOCAL_ITfFunctionProvider)) return "ITfFunctionProvider";
    return 0;
}

static void init_helper_path(void)
{
    static const WCHAR helper_name[] = L"msctf-helper.dll";
    DWORD len;
    DWORD i;
    DWORD base = 0;

    if (g_helper_path[0]) return;

    len = GetModuleFileNameW(g_instance, g_helper_path,
                             (DWORD)(sizeof(g_helper_path) / sizeof(g_helper_path[0])));
    if (!len || len >= (DWORD)(sizeof(g_helper_path) / sizeof(g_helper_path[0]))) {
        for (i = 0; helper_name[i] && i < (DWORD)(sizeof(g_helper_path) / sizeof(g_helper_path[0])) - 1; i++) {
            g_helper_path[i] = helper_name[i];
        }
        g_helper_path[i] = 0;
        return;
    }

    for (i = 0; i < len; i++) {
        if (g_helper_path[i] == L'\\' || g_helper_path[i] == L'/') {
            base = i + 1;
        }
    }
    for (i = 0; helper_name[i] && base + i < (DWORD)(sizeof(g_helper_path) / sizeof(g_helper_path[0])) - 1; i++) {
        g_helper_path[base + i] = helper_name[i];
    }
    g_helper_path[base + i] = 0;
}

static void ensure_helper(void)
{
    if (g_helper) return;
    init_helper_path();
    g_helper = LoadLibraryW(g_helper_path);
    if (!g_helper) {
        log_hr("MsctfShim: Load helper", HRESULT_FROM_WIN32(GetLastError()));
        return;
    }
    pTF_CreateThreadMgr = (PFN_TF_CreateThreadMgr)GetProcAddress(g_helper, "TF_CreateThreadMgr");
    pTF_CreateInputProcessorProfiles = (PFN_TF_CreateInputProcessorProfiles)GetProcAddress(
        g_helper, "TF_CreateInputProcessorProfiles");
    pTF_CreateLangBarItemMgr = (PFN_TF_CreateLangBarItemMgr)GetProcAddress(
        g_helper, "TF_CreateLangBarItemMgr");
    pDllGetClassObject = (PFN_DllGetClassObject)GetProcAddress(g_helper, "DllGetClassObject");
    pDllCanUnloadNow = (PFN_DllCanUnloadNow)GetProcAddress(g_helper, "DllCanUnloadNow");
}

static void ensure_oleaut32(void)
{
    static HMODULE oleaut32;

    if (pSysAllocString) return;
    if (!oleaut32) {
        oleaut32 = LoadLibraryW(L"oleaut32.dll");
    }
    if (!oleaut32) return;
    pSysAllocString = (PFN_SysAllocString)GetProcAddress(oleaut32, "SysAllocString");
    if (!pSysFreeString)
        pSysFreeString = (PFN_SysFreeString)GetProcAddress(oleaut32, "SysFreeString");
}

static void ensure_ole32(void)
{
    static HMODULE ole32;

    if (pCoRegisterClassObject && pCoCreateInstanceLocal) return;
    if (!ole32) {
        ole32 = LoadLibraryW(L"ole32.dll");
    }
    if (!ole32) return;
    if (!pCoRegisterClassObject)
        pCoRegisterClassObject = (PFN_CoRegisterClassObject)GetProcAddress(ole32, "CoRegisterClassObject");
    if (!pCoCreateInstanceLocal)
        pCoCreateInstanceLocal = (PFN_CoCreateInstanceLocal)GetProcAddress(ole32, "CoCreateInstance");
}

static void patch_atok_ns_shim_now(void)
{
    HMODULE shim = GetModuleHandleW(L"AtNsShim.dll");
    PFN_AtNsShimPatchNow patch_now;

    if (!shim)
        shim = LoadLibraryW(L"AtNsShim.dll");
    if (!shim) {
        log_hr("MsctfShim: LoadLibraryW(AtNsShim.dll) for patch", GetLastError());
        return;
    }
    patch_now = (PFN_AtNsShimPatchNow)GetProcAddress(shim, "AtNsShimPatchNow");
    if (!patch_now)
        patch_now = (PFN_AtNsShimPatchNow)GetProcAddress(shim, "AtNsShimPatchNow@0");
    if (!patch_now) {
        log_hr("MsctfShim: GetProcAddress(AtNsShimPatchNow)", GetLastError());
        return;
    }
    patch_now();
}

static HRESULT msctf_try_activate_pending_atok_tip(MsctfThreadMgrWrap *tm)
{
    ITfTextInputProcessor *tip = 0;
    HRESULT hr;

    if (!g_profile_atok_pending || !tm || !tm->active)
        return S_FALSE;
    if (g_profile_tip)
        return S_OK;

    ensure_ole32();
    if (!pCoCreateInstanceLocal) {
        log_line("MsctfShim: profile TIP load no CoCreateInstance\r\n");
        return E_FAIL;
    }

    hr = pCoCreateInstanceLocal(&CLSID_ATOK_TIP, 0, CLSCTX_INPROC_SERVER,
                                &IID_LOCAL_ITfTextInputProcessor, (void **)&tip);
    log_ptr_hr("MsctfShim: profile TIP CoCreateInstance", tip, hr);
    if (hr != S_OK || !tip)
        return hr;

    patch_atok_ns_shim_now();
    g_profile_tip = tip;
    hr = tip->lpVtbl->Activate(tip, &tm->iface, tm->client_id);
    log_ptr_hr("MsctfShim: profile TIP Activate", tip, hr);
    if (hr == S_OK)
        g_profile_atok_pending = FALSE;
    return hr;
}

typedef struct MsctfClassFactory {
    IClassFactory iface;
    LONG refs;
} MsctfClassFactory;

static ITfCategoryMgr *make_categorymgr(void);

static inline MsctfClassFactory *factory_from_iface(IClassFactory *iface)
{
    return (MsctfClassFactory *)iface;
}

static HRESULT WINAPI factory_QI(IClassFactory *iface, REFIID riid, void **ppv)
{
    if (IsEqualIID(riid, &IID_IUnknown) || IsEqualIID(riid, &IID_LOCAL_IClassFactory)) {
        *ppv = iface;
        iface->lpVtbl->AddRef(iface);
        return S_OK;
    }
    *ppv = 0;
    return E_NOINTERFACE;
}

static ULONG WINAPI factory_AddRef(IClassFactory *iface)
{
    return (ULONG)InterlockedIncrement(&factory_from_iface(iface)->refs);
}

static ULONG WINAPI factory_Release(IClassFactory *iface)
{
    MsctfClassFactory *f = factory_from_iface(iface);
    ULONG refs = (ULONG)InterlockedDecrement(&f->refs);
    if (!refs) HeapFree(GetProcessHeap(), 0, f);
    return refs;
}

static HRESULT WINAPI factory_CreateInstance(IClassFactory *iface, IUnknown *outer, REFIID riid, void **ppv)
{
    (void)iface;
    if (outer) return CLASS_E_NOAGGREGATION;
    if (!ppv) return E_POINTER;
    {
        ITfCategoryMgr *obj = make_categorymgr();
        if (!obj) return E_OUTOFMEMORY;
        if (IsEqualIID(riid, &IID_IUnknown) || IsEqualIID(riid, &IID_LOCAL_ITfCategoryMgr)) {
            *ppv = obj;
            return S_OK;
        }
        obj->lpVtbl->Release(obj);
        *ppv = 0;
        return E_NOINTERFACE;
    }
}

static HRESULT WINAPI factory_LockServer(IClassFactory *iface, BOOL lock)
{
    (void)iface; (void)lock;
    return S_OK;
}

static const IClassFactoryVtbl g_factory_vtbl = {
    factory_QI, factory_AddRef, factory_Release, factory_CreateInstance, factory_LockServer
};

static HRESULT create_factory(REFCLSID rclsid, REFIID riid, void **ppv)
{
    MsctfClassFactory *f;

    if (!ppv) return E_POINTER;
    *ppv = 0;
    if (!IsEqualCLSID(rclsid, &CLSID_LOCAL_TF_CategoryMgr)) return CLASS_E_CLASSNOTAVAILABLE;
    if (!IsEqualIID(riid, &IID_IUnknown) && !IsEqualIID(riid, &IID_LOCAL_IClassFactory)) {
        return E_NOINTERFACE;
    }
    f = (MsctfClassFactory *)HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, sizeof(*f));
    if (!f) return E_OUTOFMEMORY;
    f->iface.lpVtbl = &g_factory_vtbl;
    f->refs = 1;
    *ppv = &f->iface;
    return S_OK;
}

static HRESULT ensure_categorymgr_registration(void)
{
    IClassFactory *factory;
    HRESULT hr;

    if (g_category_registered) return S_OK;
    ensure_ole32();
    if (!pCoRegisterClassObject) return E_FAIL;
    if (!g_category_factory) {
        hr = create_factory(&CLSID_LOCAL_TF_CategoryMgr, &IID_LOCAL_IClassFactory, (void **)&factory);
        if (hr != S_OK) return hr;
        g_category_factory = factory;
    } else {
        factory = g_category_factory;
        factory->lpVtbl->AddRef(factory);
    }
    hr = pCoRegisterClassObject(&CLSID_LOCAL_TF_CategoryMgr, (LPUNKNOWN)factory, CLSCTX_INPROC_SERVER,
                               REGCLS_MULTIPLEUSE, &g_category_cookie);
    factory->lpVtbl->Release(factory);
    if (hr == S_OK) {
        g_category_registered = TRUE;
        log_hr("MsctfShim: CoRegisterClassObject CategoryMgr", hr);
    } else {
        log_hr("MsctfShim: CoRegisterClassObject CategoryMgr failed", hr);
    }
    return hr;
}

static inline MsctfProfilesWrap *profiles_from_iface(ITfInputProcessorProfiles *iface)
{
    return (MsctfProfilesWrap *)iface;
}

static inline MsctfThreadMgrWrap *tm_from_iface(ITfThreadMgr *iface)
{
    return (MsctfThreadMgrWrap *)iface;
}

static inline MsctfThreadMgrExWrap *tmex_from_iface(ITfThreadMgrEx *iface)
{
    return (MsctfThreadMgrExWrap *)iface;
}

static inline MsctfSourceWrap *source_from_iface(ITfSource *iface)
{
    return (MsctfSourceWrap *)iface;
}

static IUnknown *source_find_sink(MsctfSourceWrap *s, REFIID riid)
{
    ULONG i;
    if (!s || !riid) return 0;
    for (i = 0; i < s->sink_count; i++) {
        if (s->sinks[i].sink && IsEqualIID(&s->sinks[i].sink_iid, riid))
            return s->sinks[i].sink;
    }
    if (s->sink && IsEqualIID(&s->sink_iid, riid))
        return s->sink;
    return 0;
}

static inline MsctfKeystrokeWrap *km_from_iface(ITfKeystrokeMgr *iface)
{
    return (MsctfKeystrokeWrap *)iface;
}

static inline MsctfCompartmentMgrWrap *compartmentmgr_from_iface(ITfCompartmentMgr *iface)
{
    return (MsctfCompartmentMgrWrap *)iface;
}

static inline MsctfCompartmentWrap *compartment_from_iface(ITfCompartment *iface)
{
    return (MsctfCompartmentWrap *)iface;
}

static inline MsctfCompartmentWrap *compartment_from_source_iface(ITfSource *iface)
{
    return (MsctfCompartmentWrap *)((BYTE *)iface - (SIZE_T)&(((MsctfCompartmentWrap *)0)->source_iface));
}

static inline MsctfUIElementMgrWrap *uielementmgr_from_iface(ITfUIElementMgr *iface)
{
    return (MsctfUIElementMgrWrap *)iface;
}

static inline MsctfLangBarItemMgrWrap *langbaritemmgr_from_iface(ITfLangBarItemMgr *iface)
{
    return (MsctfLangBarItemMgrWrap *)iface;
}

static HRESULT forward_qi_with_log(IUnknown *orig, REFIID riid, void **ppv, const char *label)
{
    HRESULT hr;
    if (!orig || !ppv) return E_POINTER;
    hr = orig->lpVtbl->QueryInterface(orig, riid, ppv);
    if (riid) {
        char buf[192];
        wsprintfA(buf, "MsctfShim: %s %s %s hr=0x%08lX\r\n",
                  label,
                  iid_name(riid) ? iid_name(riid) : "unknown",
                  guid_string(riid),
                  (unsigned long)hr);
        log_line(buf);
    }
    return hr;
}

static inline MsctfProfileEnum *enum_from_iface(IEnumTfLanguageProfiles *iface)
{
    return (MsctfProfileEnum *)iface;
}

/* IEnumTfLanguageProfiles */
static HRESULT WINAPI enum_QI(IEnumTfLanguageProfiles *iface, REFIID riid, void **ppv)
{
    if (IsEqualIID(riid, &IID_IUnknown) || IsEqualIID(riid, &IID_IEnumTfLanguageProfiles)) {
        *ppv = iface;
        iface->lpVtbl->AddRef(iface);
        return S_OK;
    }
    *ppv = 0;
    return E_NOINTERFACE;
}

static ULONG WINAPI enum_AddRef(IEnumTfLanguageProfiles *iface)
{
    return (ULONG)InterlockedIncrement(&enum_from_iface(iface)->refs);
}

static ULONG WINAPI enum_Release(IEnumTfLanguageProfiles *iface)
{
    MsctfProfileEnum *e = enum_from_iface(iface);
    ULONG r = (ULONG)InterlockedDecrement(&e->refs);
    if (!r) HeapFree(GetProcessHeap(), 0, e);
    return r;
}

static HRESULT WINAPI enum_Clone(IEnumTfLanguageProfiles *iface, IEnumTfLanguageProfiles **ppEnum)
{
    MsctfProfileEnum *src = enum_from_iface(iface);
    MsctfProfileEnum *dst = (MsctfProfileEnum *)HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, sizeof(*dst));
    if (!dst) return E_OUTOFMEMORY;
    dst->iface.lpVtbl = src->iface.lpVtbl;
    dst->refs = 1;
    dst->index = src->index;
    dst->profile = src->profile;
    *ppEnum = &dst->iface;
    return S_OK;
}

static HRESULT WINAPI enum_Next(IEnumTfLanguageProfiles *iface, ULONG count, TF_LANGUAGEPROFILE *profiles, ULONG *fetched)
{
    MsctfProfileEnum *e = enum_from_iface(iface);
    if (fetched) *fetched = 0;
    if (!profiles || !count) return E_INVALIDARG;
    if (e->index > 0) return S_FALSE;
    profiles[0] = e->profile;
    e->index = 1;
    if (fetched) *fetched = 1;
    return S_OK;
}

static HRESULT WINAPI enum_Reset(IEnumTfLanguageProfiles *iface)
{
    enum_from_iface(iface)->index = 0;
    return S_OK;
}

static HRESULT WINAPI enum_Skip(IEnumTfLanguageProfiles *iface, ULONG count)
{
    MsctfProfileEnum *e = enum_from_iface(iface);
    if (count && e->index == 0) {
        e->index = 1;
        return S_OK;
    }
    return S_FALSE;
}

static const IEnumTfLanguageProfilesVtbl g_enum_vtbl = {
    enum_QI, enum_AddRef, enum_Release, enum_Clone, enum_Next, enum_Reset, enum_Skip
};

static IEnumTfLanguageProfiles *make_atok_lang_enum(void)
{
    MsctfProfileEnum *e = (MsctfProfileEnum *)HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, sizeof(*e));
    if (!e) return 0;
    e->iface.lpVtbl = &g_enum_vtbl;
    e->refs = 1;
    e->index = 0;
    ZeroMemory(&e->profile, sizeof(e->profile));
    e->profile.clsid = CLSID_ATOK_TIP;
    e->profile.langid = LANGID_JA;
    e->profile.guidProfile = GUID_ATOK_PROFILE;
    e->profile.catid = GUID_TFCAT_TIP_KEYBOARD_LOCAL;
    e->profile.fActive = TRUE;
    return &e->iface;
}

static void msctf_copy_comp_text(MsctfContextWrap *ctx, WCHAR *dst, ULONG cch)
{
    ULONG n = 0;
    if (!ctx || !dst || !cch) return;
    n = ctx->comp_len;
    if (n >= cch) n = cch - 1;
    if (n) CopyMemory(dst, ctx->comp_text, n * sizeof(WCHAR));
    dst[n] = 0;
}

static void msctf_append_key(WPARAM vk)
{
    MsctfContextWrap *ctx = g_active_context;
    BOOL was_active;
    WCHAR ch = 0;

    if (!ctx) return;
    was_active = ctx->comp_active;
    if (vk >= 'A' && vk <= 'Z') {
        ch = (WCHAR)(vk - 'A' + 'a');
        ctx->comp_active = TRUE;
    } else if (vk >= '0' && vk <= '9') {
        ch = (WCHAR)vk;
        ctx->comp_active = TRUE;
    } else if (vk == VK_BACK) {
        if (ctx->comp_len > 0) ctx->comp_len--;
        ctx->comp_text[ctx->comp_len] = 0;
        sync_context_ranges(ctx);
        ctx->comp_active = TRUE;
        return;
    } else if (vk == VK_RETURN) {
        ctx->comp_active = FALSE;
        if (was_active && ctx->source) {
            ITfContextOwnerCompositionSink *sink =
                (ITfContextOwnerCompositionSink *)source_find_sink(ctx->source, &IID_ITfContextOwnerCompositionSink);
            if (sink && sink->lpVtbl->OnEndComposition) {
                sink->lpVtbl->OnEndComposition(sink, &ctx->view->iface);
            }
        }
        return;
    } else if (vk == VK_SPACE || vk == VK_CONVERT) {
        ctx->comp_active = TRUE;
        return;
    } else {
        return;
    }

    if (ctx->comp_len + 1 < sizeof(ctx->comp_text) / sizeof(ctx->comp_text[0])) {
        ctx->comp_text[ctx->comp_len++] = ch;
        ctx->comp_text[ctx->comp_len] = 0;
        sync_context_ranges(ctx);
    }

    if (ctx->source) {
        ITfContextOwnerCompositionSink *sink =
            (ITfContextOwnerCompositionSink *)source_find_sink(ctx->source, &IID_ITfContextOwnerCompositionSink);
        BOOL ok = TRUE;

        if (sink && !was_active && ctx->comp_active && sink->lpVtbl->OnStartComposition) {
            sink->lpVtbl->OnStartComposition(sink, &ctx->view->iface, &ok);
        }
        if (sink && ctx->comp_active && sink->lpVtbl->OnUpdateComposition) {
            sink->lpVtbl->OnUpdateComposition(sink, &ctx->view->iface,
                                              ctx->range ? &ctx->range->iface : 0);
        }
        if (sink && was_active && !ctx->comp_active && sink->lpVtbl->OnEndComposition) {
            sink->lpVtbl->OnEndComposition(sink, &ctx->view->iface);
        }
    }
}

static MsctfRangeWrap *range_from_iface(ITfRange *iface) { return (MsctfRangeWrap *)iface; }
static MsctfCompositionViewWrap *view_from_iface(ITfCompositionView *iface) { return (MsctfCompositionViewWrap *)iface; }
static MsctfCompositionWrap *composition_from_iface(ITfComposition *iface) { return (MsctfCompositionWrap *)iface; }
static MsctfContextWrap *context_from_iface(ITfContext *iface) { return (MsctfContextWrap *)iface; }
static MsctfDocMgrWrap *docmgr_from_iface(ITfDocumentMgr *iface) { return (MsctfDocMgrWrap *)iface; }
static MsctfContextEnumWrap *ctxenum_from_iface(IEnumTfContexts *iface) { return (MsctfContextEnumWrap *)iface; }
static MsctfCompositionEnumWrap *compenum_from_iface(IEnumITfCompositionView *iface) { return (MsctfCompositionEnumWrap *)iface; }
static MsctfContextWrap *ctxcomp_context(ITfContextComposition *iface)
{
    return (MsctfContextWrap *)((BYTE *)iface - offsetof(MsctfContextWrap, comp_iface));
}

static MsctfContextWrap *insert_context(ITfInsertAtSelectionLocal *iface)
{
    return (MsctfContextWrap *)((BYTE *)iface - offsetof(MsctfContextWrap, insert_iface));
}

static ITfRange *range_clone_addref(MsctfRangeWrap *r);
static ITfCompositionView *view_clone_addref(MsctfCompositionViewWrap *v);
static ITfComposition *composition_clone_addref(MsctfCompositionWrap *c);
static ULONG WINAPI ctx_AddRef(ITfContext *iface);
static ULONG WINAPI ctx_Release(ITfContext *iface);
static ITfContext *context_clone_addref(MsctfContextWrap *c);
static HRESULT wrap_compartment_mgr_result(ITfCompartmentMgr *orig, void *owner, void **ppv, const char *label);
static HRESULT wrap_uielementmgr_result(ITfUIElementMgr *orig, void **ppv, const char *label);
static HRESULT wrap_langbaritemmgr_result(ITfLangBarItemMgr *orig, void *owner, void **ppv, const char *label);
static const ITfRangeVtbl g_range_vtbl;
static const ITfCompositionViewVtbl g_view_vtbl;
static const ITfCompositionVtbl g_comp_vtbl;
static const ITfContextCompositionVtbl g_ctxcomp_vtbl;
static const ITfInsertAtSelectionLocalVtbl g_insert_vtbl;
static const IEnumITfCompositionViewVtbl g_compenum_vtbl;
static const IEnumTfContextsVtbl g_ctxenum_vtbl;
static const ITfContextVtbl g_ctx_vtbl;
static const ITfEditSessionVtbl g_editsession_vtbl;
static const ITfEditRecordVtbl g_editrecord_vtbl;
static const ITfSourceVtbl g_source_vtbl;
static const ITfSourceVtbl g_compartment_source_vtbl;
static const ITfSourceSingleVtbl g_source_single_vtbl;
static const ITfCompartmentVtbl g_compartment_vtbl;
static const ITfCompartmentMgrVtbl g_compartmentmgr_vtbl;
static const ITfUIElementMgrVtbl g_uielementmgr_vtbl;
static const ITfDocumentMgrVtbl g_docmgr_vtbl;
static const ITfThreadMgrExVtbl g_tm_ex_vtbl;

static HRESULT notify_text_edit_sink_context(MsctfContextWrap *ctx, const char *label)
{
    IUnknown *punk;
    ITfTextEditSink *sink;
    MsctfEditRecordWrap *record;
    HRESULT hr;
    char buf[192];

    if (!ctx || !ctx->source) return S_FALSE;
    punk = source_find_sink(ctx->source, &IID_LOCAL_ITfTextEditSink);
    if (!punk) return S_FALSE;
    sink = (ITfTextEditSink *)punk;
    if (!sink->lpVtbl || !sink->lpVtbl->OnEndEdit) return S_FALSE;
    record = (MsctfEditRecordWrap *)HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, sizeof(*record));
    if (!record) return E_OUTOFMEMORY;
    record->iface.lpVtbl = &g_editrecord_vtbl;
    record->refs = 1;
    hr = sink->lpVtbl->OnEndEdit(sink, &ctx->iface, ctx->edit_cookie, &record->iface);
    wsprintfA(buf, "MsctfShim: TextEditSink %s OnEndEdit ec=%lu hr=0x%08lX\r\n",
              label ? label : "notify", (unsigned long)ctx->edit_cookie, (unsigned long)hr);
    log_line(buf);
    record->iface.lpVtbl->Release(&record->iface);
    return hr;
}

/* ITfRange */
static HRESULT WINAPI range_QI(ITfRange *iface, REFIID riid, void **ppv)
{
    if (IsEqualIID(riid, &IID_IUnknown) || IsEqualIID(riid, &IID_ITfRange)) {
        *ppv = iface;
        iface->lpVtbl->AddRef(iface);
        return S_OK;
    }
    *ppv = 0;
    return E_NOINTERFACE;
}

static ULONG WINAPI range_AddRef(ITfRange *iface)
{
    return (ULONG)InterlockedIncrement(&range_from_iface(iface)->refs);
}

static ULONG WINAPI range_Release(ITfRange *iface)
{
    MsctfRangeWrap *r = range_from_iface(iface);
    ULONG refs = (ULONG)InterlockedDecrement(&r->refs);
    if (!refs) HeapFree(GetProcessHeap(), 0, r);
    return refs;
}

static HRESULT WINAPI range_GetText(ITfRange *iface, TfEditCookie ec, DWORD flags, WCHAR *pchText, ULONG cchMax, ULONG *pcch)
{
    MsctfRangeWrap *r = range_from_iface(iface);
    MsctfContextWrap *ctx = r->ctx;
    ULONG start = (ULONG)r->start;
    ULONG end = (ULONG)r->end;
    ULONG n, i;
    char buf[192];
    if (!ctx || !pchText || !cchMax) return E_INVALIDARG;
    if (end > ctx->comp_len) end = ctx->comp_len;
    if (start > end) start = end;
    n = end - start;
    if (n >= cchMax) n = cchMax - 1;
    for (i = 0; i < n; i++) pchText[i] = ctx->comp_text[start + i];
    pchText[n] = 0;
    if (pcch) *pcch = n;
    wsprintfA(buf, "MsctfShim: range_GetText range=%p ec=%lu flags=0x%08lX start=%lu end=%lu comp_len=%lu got=%lu\r\n",
              iface, (unsigned long)ec, (unsigned long)flags,
              (unsigned long)start, (unsigned long)end,
              (unsigned long)ctx->comp_len, (unsigned long)n);
    log_line(buf);
    return S_OK;
}

static HRESULT WINAPI range_SetText(ITfRange *iface, TfEditCookie ec, DWORD flags, const WCHAR *pchText, LONG cch)
{
    MsctfRangeWrap *r = range_from_iface(iface);
    MsctfContextWrap *ctx = r->ctx;
    LONG i, start, end, delta;
    char buf[192];
    if (!ctx) return E_FAIL;
    if (!pchText || cch < 0) return E_INVALIDARG;
    start = r->start;
    end = r->end;
    if (start < 0) start = 0;
    if (end < start) end = start;
    if (end > (LONG)ctx->comp_len) end = (LONG)ctx->comp_len;
    delta = cch - (end - start);
    if ((ULONG)(ctx->comp_len + delta) >= sizeof(ctx->comp_text) / sizeof(ctx->comp_text[0])) return TS_E_INVALIDPOS;
    if (delta != 0 && end < (LONG)ctx->comp_len) {
        MoveMemory(&ctx->comp_text[start + cch], &ctx->comp_text[end], (ctx->comp_len - end) * sizeof(WCHAR));
    }
    for (i = 0; i < cch; i++) ctx->comp_text[start + i] = pchText[i];
    ctx->comp_len += delta;
    ctx->comp_text[ctx->comp_len] = 0;
    r->end = r->start + cch;
    sync_context_ranges(ctx);
    wsprintfA(buf, "MsctfShim: range_SetText range=%p ec=%lu flags=0x%08lX start=%ld end=%ld cch=%ld comp_len=%lu\r\n",
              iface, (unsigned long)ec, (unsigned long)flags,
              start, end, cch, (unsigned long)ctx->comp_len);
    log_line(buf);
    /* Log the actual characters ATOK writes so the romaji->kana->kanji
     * conversion is observable (the metadata above only shows lengths). */
    log_wchars_preview("MsctfShim: range_SetText text", pchText, cch);
    log_wchars_preview("MsctfShim: range_SetText comp", ctx->comp_text, (LONG)ctx->comp_len);
    return S_OK;
}

static HRESULT WINAPI range_notimpl(ITfRange *iface) { (void)iface; return E_NOTIMPL; }
static HRESULT WINAPI range_GetFormattedText(ITfRange *iface, TfEditCookie ec, IDataObject **ppDataObject) { (void)iface; (void)ec; (void)ppDataObject; return E_NOTIMPL; }
static HRESULT WINAPI range_GetEmbedded(ITfRange *iface, TfEditCookie ec, REFGUID rguidService, REFIID riid, IUnknown **ppunk) { (void)iface; (void)ec; (void)rguidService; (void)riid; (void)ppunk; return E_NOTIMPL; }
static HRESULT WINAPI range_InsertEmbedded(ITfRange *iface, TfEditCookie ec, DWORD flags, IDataObject *pDataObject) { (void)iface; (void)ec; (void)flags; (void)pDataObject; return E_NOTIMPL; }

static HRESULT WINAPI range_ShiftStart(ITfRange *iface, TfEditCookie ec, LONG cchReq, LONG *pcch, const TF_HALTCOND *pHalt)
{
    MsctfRangeWrap *r = range_from_iface(iface);
    (void)ec; (void)pHalt;
    if (pcch) *pcch = cchReq;
    r->start += cchReq;
    if (r->start < 0) r->start = 0;
    if (r->start > r->end) r->start = r->end;
    return S_OK;
}

static HRESULT WINAPI range_ShiftEnd(ITfRange *iface, TfEditCookie ec, LONG cchReq, LONG *pcch, const TF_HALTCOND *pHalt)
{
    MsctfRangeWrap *r = range_from_iface(iface);
    (void)ec; (void)pHalt;
    if (pcch) *pcch = cchReq;
    r->end += cchReq;
    if (r->end < r->start) r->end = r->start;
    return S_OK;
}

static HRESULT WINAPI range_ShiftStartToRange(ITfRange *iface, TfEditCookie ec, ITfRange *pRange, TfAnchor aPos) { (void)iface; (void)ec; (void)pRange; (void)aPos; return S_OK; }
static HRESULT WINAPI range_ShiftEndToRange(ITfRange *iface, TfEditCookie ec, ITfRange *pRange, TfAnchor aPos) { (void)iface; (void)ec; (void)pRange; (void)aPos; return S_OK; }
static HRESULT WINAPI range_ShiftStartRegion(ITfRange *iface, TfEditCookie ec, TfShiftDir dir, BOOL *pfNoRegion) { (void)iface; (void)ec; (void)dir; if (pfNoRegion) *pfNoRegion = TRUE; return S_OK; }
static HRESULT WINAPI range_ShiftEndRegion(ITfRange *iface, TfEditCookie ec, TfShiftDir dir, BOOL *pfNoRegion) { (void)iface; (void)ec; (void)dir; if (pfNoRegion) *pfNoRegion = TRUE; return S_OK; }
static HRESULT WINAPI range_IsEmpty(ITfRange *iface, TfEditCookie ec, BOOL *pfEmpty) { (void)ec; if (pfEmpty) *pfEmpty = range_from_iface(iface)->start >= range_from_iface(iface)->end; return S_OK; }
static HRESULT WINAPI range_Collapse(ITfRange *iface, TfEditCookie ec, TfAnchor aPos) { MsctfRangeWrap *r = range_from_iface(iface); (void)ec; r->end = (aPos == 0) ? r->start : r->end; return S_OK; }
static HRESULT WINAPI range_IsEqualStart(ITfRange *iface, TfEditCookie ec, ITfRange *pWith, TfAnchor aPos, BOOL *pfEqual)
{
    MsctfRangeWrap *r = range_from_iface(iface);
    MsctfRangeWrap *w = pWith ? range_from_iface(pWith) : 0;
    (void)ec; (void)aPos;
    if (pfEqual) *pfEqual = w ? (r->start == w->start) : FALSE;
    log_line("MsctfShim: range_IsEqualStart\r\n");
    return S_OK;
}
static HRESULT WINAPI range_IsEqualEnd(ITfRange *iface, TfEditCookie ec, ITfRange *pWith, TfAnchor aPos, BOOL *pfEqual)
{
    MsctfRangeWrap *r = range_from_iface(iface);
    MsctfRangeWrap *w = pWith ? range_from_iface(pWith) : 0;
    (void)ec; (void)aPos;
    if (pfEqual) *pfEqual = w ? (r->end == w->end) : FALSE;
    log_line("MsctfShim: range_IsEqualEnd\r\n");
    return S_OK;
}
static HRESULT WINAPI range_CompareStart(ITfRange *iface, TfEditCookie ec, ITfRange *pWith, TfAnchor aPos, LONG *plResult)
{
    MsctfRangeWrap *r = range_from_iface(iface);
    MsctfRangeWrap *w = pWith ? range_from_iface(pWith) : 0;
    (void)ec; (void)aPos;
    if (plResult) *plResult = w ? (r->start - w->start) : 0;
    log_line("MsctfShim: range_CompareStart\r\n");
    return S_OK;
}
static HRESULT WINAPI range_CompareEnd(ITfRange *iface, TfEditCookie ec, ITfRange *pWith, TfAnchor aPos, LONG *plResult)
{
    MsctfRangeWrap *r = range_from_iface(iface);
    MsctfRangeWrap *w = pWith ? range_from_iface(pWith) : 0;
    (void)ec; (void)aPos;
    if (plResult) *plResult = w ? (r->end - w->end) : 0;
    log_line("MsctfShim: range_CompareEnd\r\n");
    return S_OK;
}
static HRESULT WINAPI range_AdjustForInsert(ITfRange *iface, TfEditCookie ec, ULONG cchInsert, BOOL *pfInsertOk) { (void)iface; (void)ec; (void)cchInsert; if (pfInsertOk) *pfInsertOk = TRUE; return S_OK; }
static HRESULT WINAPI range_GetGravity(ITfRange *iface, TfGravity *pgStart, TfGravity *pgEnd) { (void)iface; if (pgStart) *pgStart = TF_GRAVITY_FORWARD; if (pgEnd) *pgEnd = TF_GRAVITY_FORWARD; return S_OK; }
static HRESULT WINAPI range_SetGravity(ITfRange *iface, TfEditCookie ec, TfGravity gStart, TfGravity gEnd) { (void)iface; (void)ec; (void)gStart; (void)gEnd; return S_OK; }
static HRESULT WINAPI range_Clone(ITfRange *iface, ITfRange **ppClone) { if (!ppClone) return E_POINTER; *ppClone = range_clone_addref(range_from_iface(iface)); return *ppClone ? S_OK : E_OUTOFMEMORY; }
static HRESULT WINAPI range_GetContext(ITfRange *iface, ITfContext **ppContext) { MsctfRangeWrap *r = range_from_iface(iface); if (!ppContext) return E_POINTER; *ppContext = context_clone_addref(r->ctx); log_line("MsctfShim: range_GetContext\r\n"); return *ppContext ? S_OK : E_OUTOFMEMORY; }

static const ITfRangeVtbl g_range_vtbl = {
    range_QI, range_AddRef, range_Release, range_GetText, range_SetText, range_GetFormattedText,
    range_GetEmbedded, range_InsertEmbedded, range_ShiftStart, range_ShiftEnd, range_ShiftStartToRange,
    range_ShiftEndToRange, range_ShiftStartRegion, range_ShiftEndRegion, range_IsEmpty, range_Collapse,
    range_IsEqualStart, range_IsEqualEnd, range_CompareStart, range_CompareEnd, range_AdjustForInsert,
    range_GetGravity, range_SetGravity, range_Clone, range_GetContext
};

static ITfRange *range_clone_addref(MsctfRangeWrap *r)
{
    if (!r) return 0;
    InterlockedIncrement(&r->refs);
    return &r->iface;
}

static MsctfRangeWrap *make_range(MsctfContextWrap *ctx, LONG start, LONG end)
{
    MsctfRangeWrap *r = (MsctfRangeWrap *)HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, sizeof(*r));
    if (!r) return 0;
    r->iface.lpVtbl = &g_range_vtbl;
    r->refs = 1;
    r->ctx = ctx;
    r->start = start;
    r->end = end;
    return r;
}

/* ITfCompositionView */
static HRESULT WINAPI view_QI(ITfCompositionView *iface, REFIID riid, void **ppv)
{
    if (IsEqualIID(riid, &IID_IUnknown) || IsEqualIID(riid, &IID_ITfCompositionView)) {
        *ppv = iface;
        iface->lpVtbl->AddRef(iface);
        return S_OK;
    }
    *ppv = 0;
    return E_NOINTERFACE;
}

static ULONG WINAPI view_AddRef(ITfCompositionView *iface) { return (ULONG)InterlockedIncrement(&view_from_iface(iface)->refs); }
static ULONG WINAPI view_Release(ITfCompositionView *iface)
{
    MsctfCompositionViewWrap *v = view_from_iface(iface);
    ULONG refs = (ULONG)InterlockedDecrement(&v->refs);
    if (!refs) {
        if (v->range) v->range->iface.lpVtbl->Release(&v->range->iface);
        HeapFree(GetProcessHeap(), 0, v);
    }
    return refs;
}
static HRESULT WINAPI view_GetOwnerClsid(ITfCompositionView *iface, CLSID *pclsid)
{
    (void)iface;
    if (pclsid) {
        CLSID clsid = {0};
        *pclsid = clsid;
    }
    return S_OK;
}
static HRESULT WINAPI view_GetRange(ITfCompositionView *iface, ITfRange **ppRange)
{
    MsctfCompositionViewWrap *v = view_from_iface(iface);
    if (!ppRange) return E_POINTER;
    *ppRange = range_clone_addref(v->range);
    return *ppRange ? S_OK : E_OUTOFMEMORY;
}

static const ITfCompositionViewVtbl g_view_vtbl = {
    view_QI, view_AddRef, view_Release, view_GetOwnerClsid, view_GetRange
};

static ITfCompositionView *view_clone_addref(MsctfCompositionViewWrap *v)
{
    if (!v) return 0;
    InterlockedIncrement(&v->refs);
    return &v->iface;
}

static MsctfCompositionViewWrap *make_view(MsctfContextWrap *ctx, MsctfRangeWrap *range)
{
    MsctfCompositionViewWrap *v = (MsctfCompositionViewWrap *)HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, sizeof(*v));
    if (!v) return 0;
    v->iface.lpVtbl = &g_view_vtbl;
    v->refs = 1;
    v->ctx = ctx;
    v->range = range;
    if (range) range->iface.lpVtbl->AddRef(&range->iface);
    return v;
}

/* ITfComposition */
static HRESULT WINAPI comp_QI(ITfComposition *iface, REFIID riid, void **ppv)
{
    if (IsEqualIID(riid, &IID_IUnknown) || IsEqualIID(riid, &IID_ITfComposition)) {
        *ppv = iface;
        iface->lpVtbl->AddRef(iface);
        return S_OK;
    }
    *ppv = 0;
    return E_NOINTERFACE;
}

static ULONG WINAPI comp_AddRef(ITfComposition *iface) { return (ULONG)InterlockedIncrement(&composition_from_iface(iface)->refs); }
static ULONG WINAPI comp_Release(ITfComposition *iface)
{
    MsctfCompositionWrap *c = composition_from_iface(iface);
    ULONG refs = (ULONG)InterlockedDecrement(&c->refs);
    if (!refs) {
        if (c->sink) c->sink->lpVtbl->Release(c->sink);
        if (c->range) c->range->iface.lpVtbl->Release(&c->range->iface);
        HeapFree(GetProcessHeap(), 0, c);
    }
    return refs;
}
static HRESULT WINAPI comp_GetRange(ITfComposition *iface, ITfRange **ppRange)
{
    MsctfCompositionWrap *c = composition_from_iface(iface);
    if (!ppRange) return E_POINTER;
    *ppRange = range_clone_addref(c->range);
    return *ppRange ? S_OK : E_OUTOFMEMORY;
}
static HRESULT WINAPI comp_ShiftStart(ITfComposition *iface, TfEditCookie ecWrite, ITfRange *pNewStart) { (void)iface; (void)ecWrite; (void)pNewStart; return S_OK; }
static HRESULT WINAPI comp_ShiftEnd(ITfComposition *iface, TfEditCookie ecWrite, ITfRange *pNewEnd) { (void)iface; (void)ecWrite; (void)pNewEnd; return S_OK; }
/* Mirror the finalized composition text into the backing ACP document store.
 * MsctfShim services ITfRange edits out of its own comp_text buffer rather than
 * the harness ITextStoreACP, so ATOK's converted result (e.g. U+65E5 U+672C
 * U+8A9E = 日本語) never reaches the document on its own. On commit we append
 * comp_text to the store so the committed Japanese is observable via STORE text. */
static void flush_comp_text_to_store(MsctfContextWrap *c)
{
    LONG endacp = 0;
    TS_TEXTCHANGE change;
    if (!c || !c->textstore || !c->textstore->lpVtbl || c->comp_len == 0) return;
    if (c->textstore->lpVtbl->GetEndACP)
        c->textstore->lpVtbl->GetEndACP(c->textstore, &endacp);
    if (endacp < 0) endacp = 0;
    ZeroMemory(&change, sizeof(change));
    if (c->textstore->lpVtbl->SetText) {
        HRESULT hr = c->textstore->lpVtbl->SetText(c->textstore, 0, endacp, endacp,
                                                   c->comp_text, c->comp_len, &change);
        log_wchars_preview("MsctfShim: EndComposition commit->store", c->comp_text, (LONG)c->comp_len);
        log_ptr_hr("MsctfShim: EndComposition commit->store hr", c->textstore, hr);
    }
}

static HRESULT WINAPI comp_EndComposition(ITfComposition *iface, TfEditCookie ecWrite)
{
    MsctfCompositionWrap *c = composition_from_iface(iface);
    char buf[128];
    /* A TIP voluntarily ending its own composition (ITfComposition::EndComposition)
     * must NOT receive its own ITfCompositionSink::OnCompositionTerminated --
     * that callback is reserved for terminations the TIP did not initiate
     * (external edit session / ITfContextOwnerCompositionServices::TerminateComposition).
     * ATOK re-invokes EndComposition from inside its OnCompositionTerminated
     * handler, so firing the sink here recursed forever. Just mark the
     * composition inactive (idempotent); the TIP already knows it ended. */
    if (!c->ctx || !c->ctx->comp_active) return S_OK;
    wsprintfA(buf, "MsctfShim: ITfComposition::EndComposition ec=%lu comp=%p\r\n",
              (unsigned long)ecWrite, iface);
    log_line(buf);
    c->ctx->comp_active = FALSE;
    flush_comp_text_to_store(c->ctx);
    return S_OK;
}

static const ITfCompositionVtbl g_comp_vtbl = {
    comp_QI, comp_AddRef, comp_Release, comp_GetRange, comp_ShiftStart, comp_ShiftEnd, comp_EndComposition
};

static ITfComposition *composition_clone_addref(MsctfCompositionWrap *c)
{
    if (!c) return 0;
    InterlockedIncrement(&c->refs);
    return &c->iface;
}

static MsctfCompositionWrap *make_composition(MsctfContextWrap *ctx, MsctfRangeWrap *range, ITfCompositionSink *sink)
{
    MsctfCompositionWrap *c = (MsctfCompositionWrap *)HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, sizeof(*c));
    if (!c) return 0;
    c->iface.lpVtbl = &g_comp_vtbl;
    c->refs = 1;
    c->ctx = ctx;
    c->range = range;
    c->sink = sink;
    if (range) range->iface.lpVtbl->AddRef(&range->iface);
    if (sink) sink->lpVtbl->AddRef(sink);
    return c;
}

/* IEnumITfCompositionView */
static HRESULT WINAPI compenum_QI(IEnumITfCompositionView *iface, REFIID riid, void **ppv)
{
    if (IsEqualIID(riid, &IID_IUnknown) || IsEqualIID(riid, &IID_IEnumITfCompositionView)) {
        *ppv = iface;
        iface->lpVtbl->AddRef(iface);
        return S_OK;
    }
    *ppv = 0;
    return E_NOINTERFACE;
}

static ULONG WINAPI compenum_AddRef(IEnumITfCompositionView *iface) { return (ULONG)InterlockedIncrement(&compenum_from_iface(iface)->refs); }
static ULONG WINAPI compenum_Release(IEnumITfCompositionView *iface)
{
    MsctfCompositionEnumWrap *e = compenum_from_iface(iface);
    ULONG refs = (ULONG)InterlockedDecrement(&e->refs);
    if (!refs) {
        if (e->orig) e->orig->lpVtbl->Release(e->orig);
        HeapFree(GetProcessHeap(), 0, e);
    }
    return refs;
}
static HRESULT WINAPI compenum_Clone(IEnumITfCompositionView *iface, IEnumITfCompositionView **ret)
{
    MsctfCompositionEnumWrap *src = compenum_from_iface(iface);
    MsctfCompositionEnumWrap *dst = (MsctfCompositionEnumWrap *)HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, sizeof(*dst));
    if (!dst) return E_OUTOFMEMORY;
    dst->iface.lpVtbl = &g_compenum_vtbl;
    dst->refs = 1;
    dst->ctx = src->ctx;
    dst->returned = src->returned;
    dst->orig = src->orig;
    if (dst->orig) dst->orig->lpVtbl->AddRef(dst->orig);
    *ret = &dst->iface;
    return S_OK;
}
static HRESULT WINAPI compenum_Next(IEnumITfCompositionView *iface, ULONG count, ITfCompositionView **views, ULONG *fetched)
{
    MsctfCompositionEnumWrap *e = compenum_from_iface(iface);
    if (fetched) *fetched = 0;
    if (!views || !count) return E_INVALIDARG;
    if (e->returned || !e->ctx || !e->ctx->comp_active) return S_FALSE;
    views[0] = view_clone_addref(e->ctx->view);
    if (!views[0]) return E_OUTOFMEMORY;
    e->returned = TRUE;
    if (fetched) *fetched = 1;
    return S_OK;
}
static HRESULT WINAPI compenum_Reset(IEnumITfCompositionView *iface) { compenum_from_iface(iface)->returned = FALSE; return S_OK; }
static HRESULT WINAPI compenum_Skip(IEnumITfCompositionView *iface, ULONG count) { MsctfCompositionEnumWrap *e = compenum_from_iface(iface); if (count && !e->returned) { e->returned = TRUE; return S_OK; } return S_FALSE; }

static const IEnumITfCompositionViewVtbl g_compenum_vtbl = {
    compenum_QI, compenum_AddRef, compenum_Release, compenum_Clone, compenum_Next, compenum_Reset, compenum_Skip
};

/* IEnumTfContexts */
static HRESULT WINAPI ctxenum_QI(IEnumTfContexts *iface, REFIID riid, void **ppv)
{
    if (IsEqualIID(riid, &IID_IUnknown) || IsEqualIID(riid, &IID_IEnumTfContexts)) {
        *ppv = iface;
        iface->lpVtbl->AddRef(iface);
        return S_OK;
    }
    *ppv = 0;
    return E_NOINTERFACE;
}
static ULONG WINAPI ctxenum_AddRef(IEnumTfContexts *iface) { return (ULONG)InterlockedIncrement(&ctxenum_from_iface(iface)->refs); }
static ULONG WINAPI ctxenum_Release(IEnumTfContexts *iface)
{
    MsctfContextEnumWrap *e = ctxenum_from_iface(iface);
    ULONG refs = (ULONG)InterlockedDecrement(&e->refs);
    if (!refs) {
        if (e->orig) e->orig->lpVtbl->Release(e->orig);
        HeapFree(GetProcessHeap(), 0, e);
    }
    return refs;
}
static HRESULT WINAPI ctxenum_Clone(IEnumTfContexts *iface, IEnumTfContexts **ppEnum)
{
    MsctfContextEnumWrap *src = ctxenum_from_iface(iface);
    MsctfContextEnumWrap *dst = (MsctfContextEnumWrap *)HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, sizeof(*dst));
    if (!dst) return E_OUTOFMEMORY;
    dst->iface.lpVtbl = &g_ctxenum_vtbl;
    dst->refs = 1;
    dst->ctx = src->ctx;
    dst->returned = src->returned;
    dst->orig = src->orig;
    if (dst->orig) dst->orig->lpVtbl->AddRef(dst->orig);
    *ppEnum = &dst->iface;
    return S_OK;
}
static HRESULT WINAPI ctxenum_Next(IEnumTfContexts *iface, ULONG count, ITfContext **rgContext, ULONG *pcFetched)
{
    MsctfContextEnumWrap *e = ctxenum_from_iface(iface);
    if (pcFetched) *pcFetched = 0;
    if (!rgContext || !count) return E_INVALIDARG;
    if (e->returned || !e->ctx) return S_FALSE;
    rgContext[0] = context_clone_addref(e->ctx);
    if (!rgContext[0]) return E_OUTOFMEMORY;
    e->returned = TRUE;
    if (pcFetched) *pcFetched = 1;
    return S_OK;
}
static HRESULT WINAPI ctxenum_Reset(IEnumTfContexts *iface) { ctxenum_from_iface(iface)->returned = FALSE; return S_OK; }
static HRESULT WINAPI ctxenum_Skip(IEnumTfContexts *iface, ULONG ulCount) { MsctfContextEnumWrap *e = ctxenum_from_iface(iface); if (ulCount && !e->returned) { e->returned = TRUE; return S_OK; } return S_FALSE; }

static const IEnumTfContextsVtbl g_ctxenum_vtbl = {
    ctxenum_QI, ctxenum_AddRef, ctxenum_Release, ctxenum_Clone, ctxenum_Next, ctxenum_Reset, ctxenum_Skip
};

static HRESULT WINAPI ctxcomp_QI(ITfContextComposition *iface, REFIID riid, void **ppv)
{
    if (IsEqualIID(riid, &IID_IUnknown) || IsEqualIID(riid, &IID_ITfContextComposition)) {
        *ppv = iface;
        iface->lpVtbl->AddRef(iface);
        return S_OK;
    }
    *ppv = 0;
    return E_NOINTERFACE;
}

static ULONG WINAPI ctxcomp_AddRef(ITfContextComposition *iface)
{
    return ctx_AddRef(&ctxcomp_context(iface)->iface);
}

static ULONG WINAPI ctxcomp_Release(ITfContextComposition *iface)
{
    return ctx_Release(&ctxcomp_context(iface)->iface);
}

static HRESULT WINAPI ctxcomp_StartComposition(ITfContextComposition *iface, TfEditCookie ecWrite, ITfRange *pCompositionRange,
                                               ITfCompositionSink *pSink, ITfComposition **ppComposition)
{
    MsctfContextWrap *ctx = ctxcomp_context(iface);
    (void)ecWrite;
    log_ptr2_hr("MsctfShim: ITfContextComposition::StartComposition", ctx ? &ctx->iface : 0, pCompositionRange, S_OK);
    if (!ctx || !ppComposition) return E_POINTER;
    ctx->comp_active = TRUE;
    ctx->comp_len = 0;
    ctx->comp_text[0] = 0;
    if (ctx->composition) {
        if (ctx->composition->sink) ctx->composition->sink->lpVtbl->Release(ctx->composition->sink);
        ctx->composition->sink = pSink;
        if (ctx->composition->sink) ctx->composition->sink->lpVtbl->AddRef(ctx->composition->sink);
        *ppComposition = &ctx->composition->iface;
        (*ppComposition)->lpVtbl->AddRef(*ppComposition);
        return S_OK;
    }
    return E_FAIL;
}

static HRESULT WINAPI ctxcomp_EnumCompositions(ITfContextComposition *iface, IEnumITfCompositionView **ppEnum)
{
    MsctfContextWrap *ctx = ctxcomp_context(iface);
    MsctfCompositionEnumWrap *e;
    if (!ppEnum) return E_POINTER;
    log_ptr_hr("MsctfShim: ITfContextComposition::EnumCompositions", ctx ? &ctx->iface : 0, S_OK);
    if (!ctx || !ctx->comp_active || !ctx->view) {
        *ppEnum = 0;
        return S_FALSE;
    }
    e = (MsctfCompositionEnumWrap *)HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, sizeof(*e));
    if (!e) return E_OUTOFMEMORY;
    e->iface.lpVtbl = &g_compenum_vtbl;
    e->refs = 1;
    e->ctx = ctx;
    e->returned = FALSE;
    e->orig = 0;
    *ppEnum = &e->iface;
    return S_OK;
}

static HRESULT WINAPI ctxcomp_FindComposition(ITfContextComposition *iface, TfEditCookie ecRead, ITfRange *pTestRange,
                                              IEnumITfCompositionView **ppEnum)
{
    (void)ecRead;
    (void)pTestRange;
    return ctxcomp_EnumCompositions(iface, ppEnum);
}

static HRESULT WINAPI ctxcomp_TakeOwnership(ITfContextComposition *iface, TfEditCookie ecWrite, ITfCompositionView *pComposition,
                                            ITfCompositionSink *pSink, ITfComposition **ppComposition)
{
    MsctfContextWrap *ctx = ctxcomp_context(iface);
    (void)ecWrite;
    (void)pComposition;
    log_ptr2_hr("MsctfShim: ITfContextComposition::TakeOwnership", ctx ? &ctx->iface : 0, pComposition, S_OK);
    if (!ctx || !ppComposition) return E_POINTER;
    ctx->comp_active = TRUE;
    ctx->comp_len = 0;
    ctx->comp_text[0] = 0;
    if (ctx->composition) {
        if (ctx->composition->sink) ctx->composition->sink->lpVtbl->Release(ctx->composition->sink);
        ctx->composition->sink = pSink;
        if (ctx->composition->sink) ctx->composition->sink->lpVtbl->AddRef(ctx->composition->sink);
        *ppComposition = &ctx->composition->iface;
        (*ppComposition)->lpVtbl->AddRef(*ppComposition);
        return S_OK;
    }
    return E_FAIL;
}

static const ITfContextCompositionVtbl g_ctxcomp_vtbl = {
    ctxcomp_QI, ctxcomp_AddRef, ctxcomp_Release, ctxcomp_StartComposition,
    ctxcomp_EnumCompositions, ctxcomp_FindComposition, ctxcomp_TakeOwnership
};

/* ITfInsertAtSelection */
static HRESULT WINAPI insert_QI(ITfInsertAtSelectionLocal *iface, REFIID riid, void **ppv)
{
    if (!ppv) return E_POINTER;
    *ppv = 0;
    if (IsEqualIID(riid, &IID_IUnknown) || IsEqualIID(riid, &IID_LOCAL_ITfInsertAtSelection)) {
        *ppv = iface;
        iface->lpVtbl->AddRef(iface);
        return S_OK;
    }
    return E_NOINTERFACE;
}

static ULONG WINAPI insert_AddRef(ITfInsertAtSelectionLocal *iface)
{
    return ctx_AddRef(&insert_context(iface)->iface);
}

static ULONG WINAPI insert_Release(ITfInsertAtSelectionLocal *iface)
{
    return ctx_Release(&insert_context(iface)->iface);
}

static HRESULT WINAPI insert_InsertTextAtSelection(ITfInsertAtSelectionLocal *iface, TfEditCookie ec,
                                                   DWORD flags, const WCHAR *pchText, LONG cch,
                                                   ITfRange **ppRange)
{
    MsctfContextWrap *ctx = insert_context(iface);
    TS_TEXTCHANGE change;
    LONG start = 0, end = 0;
    DWORD ts_flags = 0;
    HRESULT hr = S_OK;
    char buf[224];

    if (ppRange) *ppRange = 0;
    if (!ctx) return E_FAIL;
    if (cch < 0 && pchText) cch = (LONG)lstrlenW(pchText);
    if (cch < 0) cch = 0;
    if (!pchText && cch > 0) return E_INVALIDARG;
    if (flags & TF_IAS_NOQUERY) ts_flags |= TS_IAS_NOQUERY;
    if (flags & TF_IAS_QUERYONLY) ts_flags |= TS_IAS_QUERYONLY;

    wsprintfA(buf, "MsctfShim: ITfInsertAtSelection::InsertText ec=%lu flags=0x%08lX ts_flags=0x%08lX cch=%ld textstore=%p\r\n",
              (unsigned long)ec, (unsigned long)flags, (unsigned long)ts_flags, cch, ctx->textstore);
    log_line(buf);
    log_wchars_preview("MsctfShim: ITfInsertAtSelection text", pchText, cch);

    ZeroMemory(&change, sizeof(change));
    if (ctx->textstore && ctx->textstore->lpVtbl && ctx->textstore->lpVtbl->InsertTextAtSelection) {
        hr = ctx->textstore->lpVtbl->InsertTextAtSelection(ctx->textstore, ts_flags, pchText, (ULONG)cch,
                                                           &start, &end, &change);
        wsprintfA(buf, "MsctfShim: ITfInsertAtSelection forwarded start=%ld end=%ld change=%ld..%ld..%ld hr=0x%08lX\r\n",
                  start, end, change.acpStart, change.acpOldEnd, change.acpNewEnd, (unsigned long)hr);
        log_line(buf);
    } else {
        start = 0;
        end = cch;
        hr = S_OK;
        log_line("MsctfShim: ITfInsertAtSelection no textstore; using synthetic range state\r\n");
    }

    if (hr == S_OK && !(flags & TF_IAS_QUERYONLY)) {
        context_replace_comp_text(ctx, pchText, cch);
        ctx->comp_active = FALSE;
        notify_text_edit_sink_context(ctx, "insert");
    }
    if (hr == S_OK && ppRange && ctx->range) {
        if ((flags & TF_IAS_QUERYONLY) && end >= start) {
            ctx->range->start = 0;
            ctx->range->end = end - start;
        }
        *ppRange = range_clone_addref(ctx->range);
    }
    return hr;
}

static HRESULT WINAPI insert_InsertEmbeddedAtSelection(ITfInsertAtSelectionLocal *iface, TfEditCookie ec,
                                                       DWORD flags, IDataObject *pDataObject,
                                                       ITfRange **ppRange)
{
    char buf[192];
    (void)iface;
    if (ppRange) *ppRange = 0;
    wsprintfA(buf, "MsctfShim: ITfInsertAtSelection::InsertEmbedded ec=%lu flags=0x%08lX data=%p hr=0x%08lX\r\n",
              (unsigned long)ec, (unsigned long)flags, pDataObject, (unsigned long)E_NOTIMPL);
    log_line(buf);
    return E_NOTIMPL;
}

static const ITfInsertAtSelectionLocalVtbl g_insert_vtbl = {
    insert_QI, insert_AddRef, insert_Release,
    insert_InsertTextAtSelection, insert_InsertEmbeddedAtSelection
};

/* ITfContext */
static HRESULT WINAPI ctx_QI(ITfContext *iface, REFIID riid, void **ppv)
{
    MsctfContextWrap *c = context_from_iface(iface);
    if (IsEqualIID(riid, &IID_ITfSource)) {
        if (!c->source) {
            c->source = (MsctfSourceWrap *)HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, sizeof(MsctfSourceWrap));
            if (!c->source) return E_OUTOFMEMORY;
            c->source->iface.lpVtbl = &g_source_vtbl;
            c->source->refs = 1;
            c->source->next_cookie = 1;
            c->source->owner = c;
            c->source->owner_kind = SOURCE_OWNER_CONTEXT;
        }
        *ppv = &c->source->iface;
        c->source->iface.lpVtbl->AddRef(&c->source->iface);
        return S_OK;
    }
    if (IsEqualIID(riid, &IID_ITfContextComposition)) {
        log_ptr_hr("MsctfShim: ctx_QI ITfContextComposition req", iface, S_OK);
        *ppv = &c->comp_iface;
        c->comp_iface.lpVtbl->AddRef(&c->comp_iface);
        return S_OK;
    }
    if (IsEqualIID(riid, &IID_LOCAL_ITfInsertAtSelection)) {
        log_ptr_hr("MsctfShim: ctx_QI ITfInsertAtSelection req", iface, S_OK);
        *ppv = &c->insert_iface;
        c->insert_iface.lpVtbl->AddRef(&c->insert_iface);
        return S_OK;
    }
    if (IsEqualIID(riid, &IID_LOCAL_ITfCompartmentMgr)) {
        ITfCompartmentMgr *mgr = 0;
        HRESULT hr = c->orig->lpVtbl->QueryInterface(c->orig, riid, (void **)&mgr);
        log_ptr_hr("MsctfShim: ctx_QI ITfCompartmentMgr orig", c->orig, hr);
        if (hr != S_OK || !mgr) return hr;
        return wrap_compartment_mgr_result(mgr, c->owner, ppv, "MsctfShim: ctx_QI ITfCompartmentMgr wrapped");
    }
    if (IsEqualIID(riid, &IID_LOCAL_ITfContextOwnerServices)) {
        return forward_qi_with_log((IUnknown *)c->orig, riid, ppv, "ctx_QI service");
    }
    if (IsEqualIID(riid, &IID_LOCAL_ITfLangBarItemMgr)) {
        ITfLangBarItemMgr *mgr = 0;
        HRESULT hr = c->orig->lpVtbl->QueryInterface(c->orig, riid, (void **)&mgr);
        log_ptr_hr("MsctfShim: ctx_QI ITfLangBarItemMgr orig", c->orig, hr);
        if (hr == S_OK && mgr) {
            return wrap_langbaritemmgr_result(mgr, c->owner, ppv, "MsctfShim: ctx_QI ITfLangBarItemMgr wrapped");
        }
        return wrap_langbaritemmgr_result(0, c->owner, ppv, "MsctfShim: ctx_QI ITfLangBarItemMgr dummy");
    }
    if (IsEqualIID(riid, &IID_IUnknown) || IsEqualIID(riid, &IID_ITfContext)) {
        *ppv = iface;
        iface->lpVtbl->AddRef(iface);
        return S_OK;
    }
    if (riid) {
        char buf[256];
        wsprintfA(buf, "MsctfShim: ctx_QI fb %s c=%d l=%d o=%d i=%d\r\n",
                  guid_string(riid),
                  IsEqualIID(riid, &IID_LOCAL_ITfCompartmentMgr),
                  IsEqualIID(riid, &IID_LOCAL_ITfLangBarItemMgr),
                  IsEqualIID(riid, &IID_LOCAL_ITfContextOwnerServices),
                  IsEqualIID(riid, &IID_LOCAL_ITfInsertAtSelection));
        log_line(buf);
    }
    return c->orig->lpVtbl->QueryInterface(c->orig, riid, ppv);
}

static ULONG WINAPI ctx_AddRef(ITfContext *iface) { return (ULONG)InterlockedIncrement(&context_from_iface(iface)->refs); }
static ULONG WINAPI ctx_Release(ITfContext *iface)
{
    MsctfContextWrap *c = context_from_iface(iface);
    ULONG refs = (ULONG)InterlockedDecrement(&c->refs);
    if (!refs) {
        if (c->source) c->source->iface.lpVtbl->Release(&c->source->iface);
        if (c->textstore) c->textstore->lpVtbl->Release(c->textstore);
        if (c->orig) c->orig->lpVtbl->Release(c->orig);
        if (g_active_context == c) g_active_context = 0;
        if (c->view) c->view->iface.lpVtbl->Release(&c->view->iface);
        if (c->range) c->range->iface.lpVtbl->Release(&c->range->iface);
        if (c->composition) c->composition->iface.lpVtbl->Release(&c->composition->iface);
        HeapFree(GetProcessHeap(), 0, c);
    }
    return refs;
}

static MsctfEditSessionWrap *editsession_from_iface(ITfEditSession *iface)
{
    return (MsctfEditSessionWrap *)iface;
}

static HRESULT WINAPI editsession_QI(ITfEditSession *iface, REFIID riid, void **ppv)
{
    MsctfEditSessionWrap *w = editsession_from_iface(iface);
    if (!ppv) return E_POINTER;
    if (IsEqualIID(riid, &IID_IUnknown) || IsEqualIID(riid, &IID_LOCAL_ITfEditSession)) {
        *ppv = iface;
        iface->lpVtbl->AddRef(iface);
        return S_OK;
    }
    if (w->orig)
        return w->orig->lpVtbl->QueryInterface(w->orig, riid, ppv);
    *ppv = 0;
    return E_NOINTERFACE;
}

static ULONG WINAPI editsession_AddRef(ITfEditSession *iface)
{
    return (ULONG)InterlockedIncrement(&editsession_from_iface(iface)->refs);
}

static ULONG WINAPI editsession_Release(ITfEditSession *iface)
{
    MsctfEditSessionWrap *w = editsession_from_iface(iface);
    ULONG refs = (ULONG)InterlockedDecrement(&w->refs);
    if (!refs) {
        if (w->orig) w->orig->lpVtbl->Release(w->orig);
        HeapFree(GetProcessHeap(), 0, w);
    }
    return refs;
}

static HRESULT WINAPI editsession_DoEditSession(ITfEditSession *iface, TfEditCookie ec)
{
    MsctfEditSessionWrap *w = editsession_from_iface(iface);
    HRESULT hr = E_FAIL;
    char buf[192];
    wsprintfA(buf, "MsctfShim: editSession DoEditSession entry wrap=%p orig=%p ec=%lu tid=%lu flags=0x%08lX\r\n",
              iface, w ? w->orig : 0, (unsigned long)ec,
              w ? (unsigned long)w->tid : 0, w ? (unsigned long)w->flags : 0);
    log_line(buf);
    if (w && w->orig)
        hr = w->orig->lpVtbl->DoEditSession(w->orig, ec);
    wsprintfA(buf, "MsctfShim: editSession DoEditSession orig hr=0x%08lX\r\n", (unsigned long)hr);
    log_line(buf);
    return hr;
}

static const ITfEditSessionVtbl g_editsession_vtbl = {
    editsession_QI, editsession_AddRef, editsession_Release, editsession_DoEditSession
};

static MsctfEditSessionWrap *make_edit_session_wrap(ITfEditSession *orig, TfClientId tid, DWORD flags)
{
    MsctfEditSessionWrap *w;
    if (!orig) return 0;
    w = (MsctfEditSessionWrap *)HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, sizeof(*w));
    if (!w) return 0;
    w->iface.lpVtbl = &g_editsession_vtbl;
    w->refs = 1;
    w->orig = orig;
    w->tid = tid;
    w->flags = flags;
    orig->lpVtbl->AddRef(orig);
    return w;
}

static HRESULT WINAPI ctx_RequestEditSession(ITfContext *iface, TfClientId tid, ITfEditSession *pes, DWORD flags, HRESULT *phrSession)
{
    MsctfContextWrap *c = context_from_iface(iface);
    MsctfEditSessionWrap *wrap = 0;
    ITfEditSession *forward_pes = pes;
    HRESULT hr;
    char buf[192];
    wsprintfA(buf, "MsctfShim: ctx_RequestEditSession entry iface=%p tid=%lu pes=%p flags=0x%08lX edit_cookie=%lu\r\n",
              iface, (unsigned long)tid, pes, (unsigned long)flags,
              c ? (unsigned long)c->edit_cookie : 0);
    log_line(buf);
    wrap = make_edit_session_wrap(pes, tid, flags);
    if (wrap) forward_pes = &wrap->iface;
    hr = c->orig->lpVtbl->RequestEditSession(c->orig, tid, forward_pes, flags, phrSession);
    if (wrap) wrap->iface.lpVtbl->Release(&wrap->iface);
    wsprintfA(buf, "MsctfShim: ctx_RequestEditSession orig hr=0x%08lX phr=0x%08lX\r\n",
              (unsigned long)hr, phrSession ? (unsigned long)*phrSession : 0);
    log_line(buf);
    return hr;
}
static HRESULT WINAPI ctx_InWriteSession(ITfContext *iface, TfClientId tid, BOOL *pfWriteSession)
{
    return context_from_iface(iface)->orig->lpVtbl->InWriteSession(context_from_iface(iface)->orig, tid, pfWriteSession);
}
static HRESULT WINAPI ctx_GetSelection(ITfContext *iface, TfEditCookie ec, ULONG ulIndex, ULONG ulCount, TF_SELECTION *pSelection, ULONG *pcFetched)
{
    MsctfContextWrap *c = context_from_iface(iface);
    HRESULT hr;
    char buf[192];
    if (!pSelection || !pcFetched) return E_POINTER;
    wsprintfA(buf, "MsctfShim: ctx_GetSelection entry iface=%p ec=%lu index=%lu count=%lu sel=%p fetched=%p\r\n",
              iface, (unsigned long)ec, (unsigned long)ulIndex, (unsigned long)ulCount, pSelection, pcFetched);
    log_line(buf);
    hr = c->orig->lpVtbl->GetSelection(c->orig, ec, ulIndex, ulCount, pSelection, pcFetched);
    wsprintfA(buf, "MsctfShim: ctx_GetSelection orig hr=0x%08lX fetched=%lu range=%p\r\n",
              (unsigned long)hr, pcFetched ? (unsigned long)*pcFetched : 0, pSelection ? pSelection[0].range : 0);
    log_line(buf);
    if (hr == S_OK && *pcFetched > 0) {
        if (pSelection[0].range) {
            pSelection[0].range->lpVtbl->Release(pSelection[0].range);
        }
        pSelection[0].range = range_clone_addref(c->range);
        if (!pSelection[0].range) return E_OUTOFMEMORY;
        wsprintfA(buf, "MsctfShim: ctx_GetSelection wrapped range=%p\r\n", pSelection[0].range);
        log_line(buf);
    }
    return hr;
}
static HRESULT WINAPI ctx_SetSelection(ITfContext *iface, TfEditCookie ec, ULONG ulCount, const TF_SELECTION *pSelection)
{
    MsctfContextWrap *c = context_from_iface(iface);
    HRESULT hr;
    char buf[192];
    wsprintfA(buf, "MsctfShim: ctx_SetSelection entry iface=%p ec=%lu count=%lu range=%p\r\n",
              iface, (unsigned long)ec, (unsigned long)ulCount,
              (pSelection && ulCount) ? pSelection[0].range : 0);
    log_line(buf);
    hr = c->orig->lpVtbl->SetSelection(c->orig, ec, ulCount, pSelection);
    wsprintfA(buf, "MsctfShim: ctx_SetSelection orig hr=0x%08lX\r\n", (unsigned long)hr);
    log_line(buf);
    return hr;
}
static HRESULT WINAPI ctx_GetStart(ITfContext *iface, TfEditCookie ec, ITfRange **ppStart)
{
    MsctfContextWrap *c = context_from_iface(iface);
    char buf[160];
    if (!ppStart) return E_POINTER;
    *ppStart = range_clone_addref(c->range);
    wsprintfA(buf, "MsctfShim: ctx_GetStart ec=%lu range=%p hr=0x%08lX\r\n",
              (unsigned long)ec, *ppStart, *ppStart ? (unsigned long)S_OK : (unsigned long)E_OUTOFMEMORY);
    log_line(buf);
    return *ppStart ? S_OK : E_OUTOFMEMORY;
}
static HRESULT WINAPI ctx_GetEnd(ITfContext *iface, TfEditCookie ec, ITfRange **ppEnd)
{
    MsctfContextWrap *c = context_from_iface(iface);
    char buf[160];
    if (!ppEnd) return E_POINTER;
    *ppEnd = range_clone_addref(c->range);
    wsprintfA(buf, "MsctfShim: ctx_GetEnd ec=%lu range=%p hr=0x%08lX\r\n",
              (unsigned long)ec, *ppEnd, *ppEnd ? (unsigned long)S_OK : (unsigned long)E_OUTOFMEMORY);
    log_line(buf);
    return *ppEnd ? S_OK : E_OUTOFMEMORY;
}
static HRESULT WINAPI ctx_GetActiveView(ITfContext *iface, ITfContextView **ppView)
{
    return context_from_iface(iface)->orig->lpVtbl->GetActiveView(context_from_iface(iface)->orig, ppView);
}
static HRESULT WINAPI ctx_EnumViews(ITfContext *iface, IEnumTfContextViews **ppEnum)
{
    return context_from_iface(iface)->orig->lpVtbl->EnumViews(context_from_iface(iface)->orig, ppEnum);
}
static HRESULT WINAPI ctx_GetStatus(ITfContext *iface, TF_STATUS *pdcs)
{
    return context_from_iface(iface)->orig->lpVtbl->GetStatus(context_from_iface(iface)->orig, pdcs);
}
static HRESULT WINAPI ctx_GetProperty(ITfContext *iface, REFGUID guidProp, ITfProperty **ppProp)
{
    return context_from_iface(iface)->orig->lpVtbl->GetProperty(context_from_iface(iface)->orig, guidProp, ppProp);
}
static HRESULT WINAPI ctx_GetAppProperty(ITfContext *iface, REFGUID guidProp, ITfReadOnlyProperty **ppProp)
{
    return context_from_iface(iface)->orig->lpVtbl->GetAppProperty(context_from_iface(iface)->orig, guidProp, ppProp);
}
static HRESULT WINAPI ctx_TrackProperties(ITfContext *iface, const GUID **prgProp, ULONG cProp, const GUID **prgAppProp, ULONG cAppProp, ITfReadOnlyProperty **ppProperty)
{
    return context_from_iface(iface)->orig->lpVtbl->TrackProperties(context_from_iface(iface)->orig, prgProp, cProp, prgAppProp, cAppProp, ppProperty);
}
static HRESULT WINAPI ctx_EnumProperties(ITfContext *iface, IEnumTfProperties **ppEnum)
{
    return context_from_iface(iface)->orig->lpVtbl->EnumProperties(context_from_iface(iface)->orig, ppEnum);
}
static HRESULT WINAPI ctx_GetDocumentMgr(ITfContext *iface, ITfDocumentMgr **ppDm)
{
    MsctfContextWrap *c = context_from_iface(iface);
    if (!ppDm) return E_POINTER;
    if (!c->owner) return c->orig->lpVtbl->GetDocumentMgr(c->orig, ppDm);
    *ppDm = &c->owner->iface;
    (*ppDm)->lpVtbl->AddRef(*ppDm);
    return S_OK;
}
static HRESULT WINAPI ctx_CreateRangeBackup(ITfContext *iface, TfEditCookie ec, ITfRange *pRange, ITfRangeBackup **ppBackup)
{
    return context_from_iface(iface)->orig->lpVtbl->CreateRangeBackup(context_from_iface(iface)->orig, ec, pRange, ppBackup);
}

static const ITfContextVtbl g_ctx_vtbl = {
    ctx_QI, ctx_AddRef, ctx_Release, ctx_RequestEditSession, ctx_InWriteSession, ctx_GetSelection, ctx_SetSelection,
    ctx_GetStart, ctx_GetEnd, ctx_GetActiveView, ctx_EnumViews, ctx_GetStatus, ctx_GetProperty, ctx_GetAppProperty,
    ctx_TrackProperties, ctx_EnumProperties, ctx_GetDocumentMgr, ctx_CreateRangeBackup
};

static ITfContext *context_clone_addref(MsctfContextWrap *c)
{
    if (!c) return 0;
    InterlockedIncrement(&c->refs);
    return &c->iface;
}

static MsctfContextWrap *make_context(MsctfDocMgrWrap *owner, ITfContext *orig, IUnknown *punk_textstore)
{
    MsctfContextWrap *c = (MsctfContextWrap *)HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, sizeof(*c));
    HRESULT ts_hr = E_POINTER;
    char buf[192];
    if (!c) return 0;
    c->iface.lpVtbl = &g_ctx_vtbl;
    c->insert_iface.lpVtbl = &g_insert_vtbl;
    c->refs = 1;
    c->orig = orig;
    c->owner = owner;
    c->comp_active = FALSE;
    c->comp_len = 0;
    c->comp_text[0] = 0;
    c->source = 0;
    c->range = make_range(c, 0, 0);
    c->view = make_view(c, c->range);
    c->composition = make_composition(c, c->range, 0);
    c->comp_iface.lpVtbl = &g_ctxcomp_vtbl;
    if (punk_textstore && punk_textstore->lpVtbl && punk_textstore->lpVtbl->QueryInterface) {
        ts_hr = punk_textstore->lpVtbl->QueryInterface(punk_textstore, &IID_LOCAL_ITextStoreACP, (void **)&c->textstore);
        wsprintfA(buf, "MsctfShim: CreateContext textstore QI hr=0x%08lX textstore=%p\r\n",
                  (unsigned long)ts_hr, c->textstore);
        log_line(buf);
    }
    if (!c->range || !c->view || !c->composition) {
        if (c->composition) c->composition->iface.lpVtbl->Release(&c->composition->iface);
        if (c->view) c->view->iface.lpVtbl->Release(&c->view->iface);
        if (c->range) c->range->iface.lpVtbl->Release(&c->range->iface);
        if (c->textstore) c->textstore->lpVtbl->Release(c->textstore);
        if (c->orig) c->orig->lpVtbl->Release(c->orig);
        HeapFree(GetProcessHeap(), 0, c);
        return 0;
    }
    g_active_context = c;
    return c;
}

/* ITfDocumentMgr */
static HRESULT WINAPI docmgr_QI(ITfDocumentMgr *iface, REFIID riid, void **ppv)
{
    MsctfDocMgrWrap *d = docmgr_from_iface(iface);
    if (IsEqualIID(riid, &IID_IUnknown) || IsEqualIID(riid, &IID_ITfDocumentMgr)) {
        *ppv = iface;
        iface->lpVtbl->AddRef(iface);
        return S_OK;
    }
    if (IsEqualIID(riid, &IID_LOCAL_ITfCompartmentMgr)) {
        ITfCompartmentMgr *mgr = 0;
        HRESULT hr = d->orig->lpVtbl->QueryInterface(d->orig, riid, (void **)&mgr);
        log_ptr_hr("MsctfShim: docmgr_QI ITfCompartmentMgr orig", d->orig, hr);
        if (hr != S_OK || !mgr) return hr;
        return wrap_compartment_mgr_result(mgr, d, ppv, "MsctfShim: docmgr_QI ITfCompartmentMgr wrapped");
    }
    if (IsEqualIID(riid, &IID_LOCAL_ITfContextOwnerServices)) {
        return forward_qi_with_log((IUnknown *)d->orig, riid, ppv, "docmgr_QI service");
    }
    if (IsEqualIID(riid, &IID_LOCAL_ITfLangBarItemMgr)) {
        ITfLangBarItemMgr *mgr = 0;
        HRESULT hr = d->orig->lpVtbl->QueryInterface(d->orig, riid, (void **)&mgr);
        log_ptr_hr("MsctfShim: docmgr_QI ITfLangBarItemMgr orig", d->orig, hr);
        if (hr == S_OK && mgr) {
            return wrap_langbaritemmgr_result(mgr, d, ppv, "MsctfShim: docmgr_QI ITfLangBarItemMgr wrapped");
        }
        return wrap_langbaritemmgr_result(0, d, ppv, "MsctfShim: docmgr_QI ITfLangBarItemMgr dummy");
    }
    if (riid) {
        char buf[256];
        wsprintfA(buf, "MsctfShim: docmgr_QI fb %s c=%d l=%d o=%d\r\n",
                  guid_string(riid),
                  IsEqualIID(riid, &IID_LOCAL_ITfCompartmentMgr),
                  IsEqualIID(riid, &IID_LOCAL_ITfLangBarItemMgr),
                  IsEqualIID(riid, &IID_LOCAL_ITfContextOwnerServices));
        log_line(buf);
    }
    *ppv = 0;
    return E_NOINTERFACE;
}
static ULONG WINAPI docmgr_AddRef(ITfDocumentMgr *iface) { return (ULONG)InterlockedIncrement(&docmgr_from_iface(iface)->refs); }
static ULONG WINAPI docmgr_Release(ITfDocumentMgr *iface)
{
    MsctfDocMgrWrap *d = docmgr_from_iface(iface);
    ULONG refs = (ULONG)InterlockedDecrement(&d->refs);
    if (!refs) {
        if (d->orig) d->orig->lpVtbl->Release(d->orig);
        HeapFree(GetProcessHeap(), 0, d);
    }
    return refs;
}
static HRESULT WINAPI docmgr_CreateContext(ITfDocumentMgr *iface, TfClientId tidOwner, DWORD dwFlags, IUnknown *punk, ITfContext **ppic, TfEditCookie *pecTextStore)
{
    MsctfDocMgrWrap *d = docmgr_from_iface(iface);
    ITfContext *orig_ctx = 0;
    HRESULT hr = d->orig->lpVtbl->CreateContext(d->orig, tidOwner, dwFlags, punk, &orig_ctx, pecTextStore);
    log_ptr2_hr("MsctfShim: CreateContext orig", iface, orig_ctx, hr);
    if (hr != S_OK || !orig_ctx) return hr;
    if (orig_ctx->lpVtbl && orig_ctx->lpVtbl->QueryInterface) {
        void *probe = 0;
        HRESULT probe_hr = orig_ctx->lpVtbl->QueryInterface(orig_ctx, &IID_ITfContextComposition, &probe);
        log_ptr_hr("MsctfShim: CreateContext probe ITfContextComposition", orig_ctx, probe_hr);
        if (probe) {
            ((IUnknown *)probe)->lpVtbl->Release((IUnknown *)probe);
        }
    }
    d->top = make_context(d, orig_ctx, punk);
    if (!d->top) {
        orig_ctx->lpVtbl->Release(orig_ctx);
        return E_OUTOFMEMORY;
    }
    if (pecTextStore) d->top->edit_cookie = *pecTextStore;
    log_ptr2_hr("MsctfShim: CreateContext wrapped", orig_ctx, &d->top->iface, S_OK);
    *ppic = &d->top->iface;
    return S_OK;
}
static HRESULT WINAPI docmgr_Push(ITfDocumentMgr *iface, ITfContext *pic)
{
    MsctfDocMgrWrap *d = docmgr_from_iface(iface);
    log_ptr2_hr("MsctfShim: Push", iface, pic, S_OK);
    if (pic && pic->lpVtbl == &g_ctx_vtbl && context_from_iface(pic)->orig) pic = context_from_iface(pic)->orig;
    {
        HRESULT hr = d->orig->lpVtbl->Push(d->orig, pic);
        log_ptr2_hr("MsctfShim: Push forwarded", d->orig, pic, hr);
        return hr;
    }
}
static HRESULT WINAPI docmgr_Pop(ITfDocumentMgr *iface, DWORD dwFlags)
{
    return docmgr_from_iface(iface)->orig->lpVtbl->Pop(docmgr_from_iface(iface)->orig, dwFlags);
}
static HRESULT WINAPI docmgr_GetTop(ITfDocumentMgr *iface, ITfContext **ppic)
{
    MsctfDocMgrWrap *d = docmgr_from_iface(iface);
    ITfContext *orig_ctx = 0;
    HRESULT hr = d->orig->lpVtbl->GetTop(d->orig, &orig_ctx);
    if (hr != S_OK || !orig_ctx) return hr;
    if (d->top && d->top->orig == orig_ctx) {
        *ppic = &d->top->iface;
        (*ppic)->lpVtbl->AddRef(*ppic);
        orig_ctx->lpVtbl->Release(orig_ctx);
        log_ptr2_hr("MsctfShim: GetTop wrapped", d->orig, *ppic, S_OK);
        return S_OK;
    }
    d->top = make_context(d, orig_ctx, 0);
    if (!d->top) {
        orig_ctx->lpVtbl->Release(orig_ctx);
        return E_OUTOFMEMORY;
    }
    *ppic = &d->top->iface;
    log_ptr2_hr("MsctfShim: GetTop new wrap", d->orig, *ppic, S_OK);
    return S_OK;
}
static HRESULT WINAPI docmgr_GetBase(ITfDocumentMgr *iface, ITfContext **ppic)
{
    MsctfDocMgrWrap *d = docmgr_from_iface(iface);
    ITfContext *orig_ctx = 0;
    HRESULT hr = d->orig->lpVtbl->GetBase(d->orig, &orig_ctx);
    if (hr != S_OK || !orig_ctx) return hr;
    if (d->top && d->top->orig == orig_ctx) {
        *ppic = &d->top->iface;
        (*ppic)->lpVtbl->AddRef(*ppic);
        orig_ctx->lpVtbl->Release(orig_ctx);
        log_ptr2_hr("MsctfShim: GetBase wrapped", d->orig, *ppic, S_OK);
        return S_OK;
    }
    d->top = make_context(d, orig_ctx, 0);
    if (!d->top) {
        orig_ctx->lpVtbl->Release(orig_ctx);
        return E_OUTOFMEMORY;
    }
    *ppic = &d->top->iface;
    log_ptr2_hr("MsctfShim: GetBase new wrap", d->orig, *ppic, S_OK);
    return S_OK;
}
static HRESULT WINAPI docmgr_EnumContexts(ITfDocumentMgr *iface, IEnumTfContexts **ppEnum)
{
    MsctfDocMgrWrap *d = docmgr_from_iface(iface);
    IEnumTfContexts *orig_enum = 0;
    HRESULT hr = d->orig->lpVtbl->EnumContexts(d->orig, &orig_enum);
    if (hr != S_OK || !orig_enum) return hr;
    MsctfContextEnumWrap *e = (MsctfContextEnumWrap *)HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, sizeof(*e));
    if (!e) {
        orig_enum->lpVtbl->Release(orig_enum);
        return E_OUTOFMEMORY;
    }
    e->iface.lpVtbl = &g_ctxenum_vtbl;
    e->refs = 1;
    e->ctx = d->top;
    e->orig = orig_enum;
    *ppEnum = &e->iface;
    return S_OK;
}

static const ITfDocumentMgrVtbl g_docmgr_vtbl = {
    docmgr_QI, docmgr_AddRef, docmgr_Release, docmgr_CreateContext, docmgr_Push, docmgr_Pop,
    docmgr_GetTop, docmgr_GetBase, docmgr_EnumContexts
};

static ITfDocumentMgr *docmgr_clone_addref(MsctfDocMgrWrap *d)
{
    if (!d) return 0;
    InterlockedIncrement(&d->refs);
    return &d->iface;
}

static MsctfDocMgrWrap *make_docmgr(ITfDocumentMgr *orig)
{
    MsctfDocMgrWrap *d = (MsctfDocMgrWrap *)HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, sizeof(*d));
    if (!d) return 0;
    d->iface.lpVtbl = &g_docmgr_vtbl;
    d->refs = 1;
    d->orig = orig;
    g_active_context = 0;
    return d;
}

/* ITfInputProcessorProfiles */
static HRESULT WINAPI profiles_QI(ITfInputProcessorProfiles *iface, REFIID riid, void **ppv)
{
    if (IsEqualIID(riid, &IID_IUnknown) || IsEqualIID(riid, &IID_ITfInputProcessorProfiles)) {
        *ppv = iface;
        iface->lpVtbl->AddRef(iface);
        return S_OK;
    }
    *ppv = 0;
    return E_NOINTERFACE;
}

static ULONG WINAPI profiles_AddRef(ITfInputProcessorProfiles *iface)
{
    return (ULONG)InterlockedIncrement(&profiles_from_iface(iface)->refs);
}

static ULONG WINAPI profiles_Release(ITfInputProcessorProfiles *iface)
{
    MsctfProfilesWrap *p = profiles_from_iface(iface);
    ULONG r = (ULONG)InterlockedDecrement(&p->refs);
    if (!r) {
        if (p->orig) p->orig->lpVtbl->Release(p->orig);
        HeapFree(GetProcessHeap(), 0, p);
    }
    return r;
}

static HRESULT WINAPI profiles_Register(ITfInputProcessorProfiles *iface, REFCLSID rclsid)
{
    return profiles_from_iface(iface)->orig->lpVtbl->Register(profiles_from_iface(iface)->orig, rclsid);
}

static HRESULT WINAPI profiles_Unregister(ITfInputProcessorProfiles *iface, REFCLSID rclsid)
{
    return profiles_from_iface(iface)->orig->lpVtbl->Unregister(profiles_from_iface(iface)->orig, rclsid);
}

static HRESULT WINAPI profiles_AddLanguageProfile(ITfInputProcessorProfiles *iface, REFCLSID rclsid, LANGID langid,
                                                  REFGUID guidProfile, const WCHAR *desc, ULONG cchDesc,
                                                  const WCHAR *iconFile, ULONG cchFile, ULONG iconIndex)
{
    return profiles_from_iface(iface)->orig->lpVtbl->AddLanguageProfile(
        profiles_from_iface(iface)->orig, rclsid, langid, guidProfile, desc, cchDesc, iconFile, cchFile, iconIndex);
}

static HRESULT WINAPI profiles_RemoveLanguageProfile(ITfInputProcessorProfiles *iface, REFCLSID rclsid, LANGID langid,
                                                     REFGUID guidProfile)
{
    return profiles_from_iface(iface)->orig->lpVtbl->RemoveLanguageProfile(
        profiles_from_iface(iface)->orig, rclsid, langid, guidProfile);
}

static HRESULT WINAPI profiles_EnumInputProcessorInfo(ITfInputProcessorProfiles *iface, IEnumGUID **ppEnum)
{
    return profiles_from_iface(iface)->orig->lpVtbl->EnumInputProcessorInfo(profiles_from_iface(iface)->orig, ppEnum);
}

static HRESULT WINAPI profiles_GetDefaultLanguageProfile(ITfInputProcessorProfiles *iface, LANGID langid,
                                                         REFGUID catid, CLSID *pclsid, GUID *pguidProfile)
{
    if (langid == LANGID_JA && IsEqualGUID(catid, &GUID_TFCAT_TIP_KEYBOARD_LOCAL)) {
        if (pclsid) *pclsid = CLSID_ATOK_TIP;
        if (pguidProfile) *pguidProfile = GUID_ATOK_PROFILE;
        return S_OK;
    }
    return profiles_from_iface(iface)->orig->lpVtbl->GetDefaultLanguageProfile(
        profiles_from_iface(iface)->orig, langid, catid, pclsid, pguidProfile);
}

static HRESULT WINAPI profiles_SetDefaultLanguageProfile(ITfInputProcessorProfiles *iface, LANGID langid,
                                                         REFCLSID rclsid, REFGUID guidProfiles)
{
    return profiles_from_iface(iface)->orig->lpVtbl->SetDefaultLanguageProfile(
        profiles_from_iface(iface)->orig, langid, rclsid, guidProfiles);
}

static HRESULT WINAPI profiles_ActivateLanguageProfile(ITfInputProcessorProfiles *iface, REFCLSID rclsid, LANGID langid,
                                                       REFGUID guidProfiles)
{
    MsctfProfilesWrap *p = profiles_from_iface(iface);
    if (IsEqualCLSID(rclsid, &CLSID_ATOK_TIP) && (langid == LANGID_JA || p->current_lang == langid)) {
        p->active = TRUE;
        p->active_profile.clsid = CLSID_ATOK_TIP;
        p->active_profile.langid = LANGID_JA;
        p->active_profile.guidProfile = guidProfiles ? *guidProfiles : GUID_ATOK_PROFILE;
        p->active_profile.catid = GUID_TFCAT_TIP_KEYBOARD_LOCAL;
        p->active_profile.fActive = TRUE;
        g_profile_atok_pending = TRUE;
        log_line("MsctfShim: ActivateLanguageProfile ATOK shim (pending TIP load)\r\n");
        if (g_active_tm)
            msctf_try_activate_pending_atok_tip(g_active_tm);
        return S_OK;
    }
    return p->orig->lpVtbl->ActivateLanguageProfile(p->orig, rclsid, langid, guidProfiles);
}

static HRESULT WINAPI profiles_GetActiveLanguageProfile(ITfInputProcessorProfiles *iface, REFCLSID rclsid, LANGID *plangid,
                                                         GUID *pguidProfile)
{
    MsctfProfilesWrap *p = profiles_from_iface(iface);
    if (p->active && rclsid && IsEqualCLSID(rclsid, &CLSID_ATOK_TIP)) {
        if (plangid) *plangid = LANGID_JA;
        if (pguidProfile) *pguidProfile = GUID_ATOK_PROFILE;
        return S_OK;
    }
    return p->orig->lpVtbl->GetActiveLanguageProfile(p->orig, rclsid, plangid, pguidProfile);
}

static HRESULT WINAPI profiles_GetLanguageProfileDescription(ITfInputProcessorProfiles *iface, REFCLSID rclsid,
                                                             LANGID langid, REFGUID guidProfile, BSTR *pbstrProfile)
{
    return profiles_from_iface(iface)->orig->lpVtbl->GetLanguageProfileDescription(
        profiles_from_iface(iface)->orig, rclsid, langid, guidProfile, pbstrProfile);
}

static HRESULT WINAPI profiles_GetCurrentLanguage(ITfInputProcessorProfiles *iface, LANGID *plangid)
{
    if (!plangid) return E_INVALIDARG;
    *plangid = profiles_from_iface(iface)->current_lang;
    return S_OK;
}

static HRESULT WINAPI profiles_ChangeCurrentLanguage(ITfInputProcessorProfiles *iface, LANGID langid)
{
    profiles_from_iface(iface)->current_lang = langid;
    return S_OK;
}

static HRESULT WINAPI profiles_GetLanguageList(ITfInputProcessorProfiles *iface, LANGID **ppLangId, ULONG *pulCount)
{
    return profiles_from_iface(iface)->orig->lpVtbl->GetLanguageList(profiles_from_iface(iface)->orig, ppLangId, pulCount);
}

static HRESULT WINAPI profiles_EnumLanguageProfiles(ITfInputProcessorProfiles *iface, LANGID langid, IEnumTfLanguageProfiles **ppEnum)
{
    if (langid == 0 || langid == LANGID_JA) {
        if (!ppEnum) return E_INVALIDARG;
        *ppEnum = make_atok_lang_enum();
        return *ppEnum ? S_OK : E_OUTOFMEMORY;
    }
    return profiles_from_iface(iface)->orig->lpVtbl->EnumLanguageProfiles(
        profiles_from_iface(iface)->orig, langid, ppEnum);
}

static HRESULT WINAPI profiles_EnableLanguageProfile(ITfInputProcessorProfiles *iface, REFCLSID rclsid, LANGID langid,
                                                     REFGUID guidProfile, BOOL fEnable)
{
    return profiles_from_iface(iface)->orig->lpVtbl->EnableLanguageProfile(
        profiles_from_iface(iface)->orig, rclsid, langid, guidProfile, fEnable);
}

static HRESULT WINAPI profiles_IsEnabledLanguageProfile(ITfInputProcessorProfiles *iface, REFCLSID rclsid, LANGID langid,
                                                        REFGUID guidProfile, BOOL *pfEnable)
{
    return profiles_from_iface(iface)->orig->lpVtbl->IsEnabledLanguageProfile(
        profiles_from_iface(iface)->orig, rclsid, langid, guidProfile, pfEnable);
}

static HRESULT WINAPI profiles_EnableLanguageProfileByDefault(ITfInputProcessorProfiles *iface, REFCLSID rclsid, LANGID langid,
                                                              REFGUID guidProfile, BOOL fEnable)
{
    return profiles_from_iface(iface)->orig->lpVtbl->EnableLanguageProfileByDefault(
        profiles_from_iface(iface)->orig, rclsid, langid, guidProfile, fEnable);
}

static HRESULT WINAPI profiles_SubstituteKeyboardLayout(ITfInputProcessorProfiles *iface, REFCLSID rclsid, LANGID langid,
                                                        REFGUID guidProfile, HKL hKL)
{
    return profiles_from_iface(iface)->orig->lpVtbl->SubstituteKeyboardLayout(
        profiles_from_iface(iface)->orig, rclsid, langid, guidProfile, hKL);
}

static const ITfInputProcessorProfilesVtbl g_profiles_vtbl = {
    profiles_QI, profiles_AddRef, profiles_Release,
    profiles_Register, profiles_Unregister, profiles_AddLanguageProfile, profiles_RemoveLanguageProfile,
    profiles_EnumInputProcessorInfo, profiles_GetDefaultLanguageProfile, profiles_SetDefaultLanguageProfile,
    profiles_ActivateLanguageProfile, profiles_GetActiveLanguageProfile, profiles_GetLanguageProfileDescription,
    profiles_GetCurrentLanguage, profiles_ChangeCurrentLanguage, profiles_GetLanguageList,
    profiles_EnumLanguageProfiles, profiles_EnableLanguageProfile, profiles_IsEnabledLanguageProfile,
    profiles_EnableLanguageProfileByDefault, profiles_SubstituteKeyboardLayout
};

/* ITfEditRecord */
static inline MsctfEditRecordWrap *editrecord_from_iface(ITfEditRecord *iface)
{
    return (MsctfEditRecordWrap *)iface;
}

static HRESULT WINAPI editrecord_QI(ITfEditRecord *iface, REFIID riid, void **ppv)
{
    if (!ppv) return E_POINTER;
    if (IsEqualIID(riid, &IID_IUnknown) || IsEqualIID(riid, &IID_LOCAL_ITfEditRecord)) {
        *ppv = iface;
        iface->lpVtbl->AddRef(iface);
        return S_OK;
    }
    *ppv = 0;
    return E_NOINTERFACE;
}

static ULONG WINAPI editrecord_AddRef(ITfEditRecord *iface)
{
    return (ULONG)InterlockedIncrement(&editrecord_from_iface(iface)->refs);
}

static ULONG WINAPI editrecord_Release(ITfEditRecord *iface)
{
    MsctfEditRecordWrap *r = editrecord_from_iface(iface);
    ULONG refs = (ULONG)InterlockedDecrement(&r->refs);
    if (!refs) HeapFree(GetProcessHeap(), 0, r);
    return refs;
}

static HRESULT WINAPI editrecord_GetSelectionStatus(ITfEditRecord *iface, BOOL *changed)
{
    (void)iface;
    if (!changed) return E_POINTER;
    *changed = FALSE;
    log_line("MsctfShim: EditRecord GetSelectionStatus changed=0\r\n");
    return S_OK;
}

static HRESULT WINAPI editrecord_GetTextAndPropertyUpdates(ITfEditRecord *iface, DWORD flags, const GUID **props, ULONG count, IEnumTfRanges **ret)
{
    char buf[160];
    (void)iface;
    (void)props;
    if (ret) *ret = 0;
    wsprintfA(buf, "MsctfShim: EditRecord GetTextAndPropertyUpdates flags=0x%08lX count=%lu\r\n",
              (unsigned long)flags, (unsigned long)count);
    log_line(buf);
    return S_FALSE;
}

static const ITfEditRecordVtbl g_editrecord_vtbl = {
    editrecord_QI, editrecord_AddRef, editrecord_Release,
    editrecord_GetSelectionStatus, editrecord_GetTextAndPropertyUpdates
};

static HRESULT notify_text_edit_sink_initial(MsctfSourceWrap *s, IUnknown *punk)
{
    MsctfContextWrap *ctx;
    MsctfEditRecordWrap *record;
    ITfTextEditSink *sink = (ITfTextEditSink *)punk;
    HRESULT hr;
    char buf[160];

    if (!s || s->owner_kind != SOURCE_OWNER_CONTEXT || !s->owner || !sink || !sink->lpVtbl || !sink->lpVtbl->OnEndEdit)
        return S_FALSE;
    ctx = (MsctfContextWrap *)s->owner;
    record = (MsctfEditRecordWrap *)HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, sizeof(*record));
    if (!record) return E_OUTOFMEMORY;
    record->iface.lpVtbl = &g_editrecord_vtbl;
    record->refs = 1;
    hr = sink->lpVtbl->OnEndEdit(sink, &ctx->iface, ctx->edit_cookie, &record->iface);
    wsprintfA(buf, "MsctfShim: TextEditSink initial OnEndEdit ec=%lu hr=0x%08lX\r\n",
              (unsigned long)ctx->edit_cookie, (unsigned long)hr);
    log_line(buf);
    record->iface.lpVtbl->Release(&record->iface);
    return hr;
}

/* ITfSource proxy */
static HRESULT WINAPI source_QI(ITfSource *iface, REFIID riid, void **ppv)
{
    if (IsEqualIID(riid, &IID_IUnknown) || IsEqualIID(riid, &IID_ITfSource)) {
        *ppv = iface;
        iface->lpVtbl->AddRef(iface);
        return S_OK;
    }
    *ppv = 0;
    return E_NOINTERFACE;
}

static ULONG WINAPI source_AddRef(ITfSource *iface)
{
    return (ULONG)InterlockedIncrement(&source_from_iface(iface)->refs);
}

static ULONG WINAPI source_Release(ITfSource *iface)
{
    MsctfSourceWrap *s = source_from_iface(iface);
    ULONG r = (ULONG)InterlockedDecrement(&s->refs);
    if (!r) {
        ULONG i;
        for (i = 0; i < s->sink_count; i++) {
            if (s->sinks[i].sink)
                s->sinks[i].sink->lpVtbl->Release(s->sinks[i].sink);
        }
        if (s->sink) s->sink->lpVtbl->Release(s->sink);
        HeapFree(GetProcessHeap(), 0, s);
    }
    return r;
}

static HRESULT WINAPI source_AdviseSink(ITfSource *iface, REFIID riid, IUnknown *punk, DWORD *pdwCookie)
{
    MsctfSourceWrap *s = source_from_iface(iface);
    MsctfThreadMgrWrap *tm = 0;
    MsctfContextWrap *ctx = 0;
    char buf[192];
    if (!punk || !pdwCookie) return E_POINTER;
    if (s->sink_count >= MSCTF_SOURCE_SINK_MAX) return E_OUTOFMEMORY;
    s->sinks[s->sink_count].sink = punk;
    s->sinks[s->sink_count].sink_iid = riid ? *riid : IID_IUnknown;
    s->sinks[s->sink_count].cookie = s->next_cookie++;
    if (!s->sinks[s->sink_count].cookie)
        s->sinks[s->sink_count].cookie = s->next_cookie++;
    punk->lpVtbl->AddRef(punk);
    *pdwCookie = s->sinks[s->sink_count].cookie;
    if (!s->sink) {
        s->sink = punk;
        s->sink->lpVtbl->AddRef(s->sink);
        if (riid) s->sink_iid = *riid;
        s->cookie = *pdwCookie;
    }
    s->sink_count++;
    wsprintfA(buf, "MsctfShim: AdviseSink kind=%s riid=%s sink=%p cookie=%lu owner=%d\r\n",
              iid_name(riid) ? iid_name(riid) : "unknown", guid_string(riid), punk,
              (unsigned long)*pdwCookie, s->owner_kind);
    log_line(buf);
    if (riid && IsEqualIID(riid, &IID_LOCAL_ITfThreadMgrEventSink)) {
        ITfThreadMgrEventSink *sink = (ITfThreadMgrEventSink *)punk;
        ITfDocumentMgr *focus = 0;
        ITfDocumentMgr *wrapped_focus = 0;
        ITfDocumentMgr *wrapped_prev = 0;
        HRESULT focus_hr = E_FAIL;
        HRESULT sink_hr = E_FAIL;
        if (s->owner_kind == SOURCE_OWNER_THREADMGR) {
            tm = (MsctfThreadMgrWrap *)s->owner;
            if (tm && tm->orig) {
                focus_hr = tm->orig->lpVtbl->GetFocus(tm->orig, &focus);
                if (focus && tm->docmgr && tm->docmgr->orig == focus) {
                    wrapped_focus = &tm->docmgr->iface;
                } else {
                    wrapped_focus = focus;
                }
            }
        } else if (s->owner_kind == SOURCE_OWNER_CONTEXT) {
            ctx = (MsctfContextWrap *)s->owner;
        }
        if (sink->lpVtbl && sink->lpVtbl->OnSetFocus && wrapped_focus) {
            wsprintfA(buf, "MsctfShim: threadmgr sink OnSetFocus focus=%p prev=%p getfocus_hr=0x%08lX\r\n",
                      wrapped_focus, wrapped_prev, (unsigned long)focus_hr);
            log_line(buf);
            sink_hr = sink->lpVtbl->OnSetFocus(sink, wrapped_focus, wrapped_prev);
            wsprintfA(buf, "MsctfShim: threadmgr sink OnSetFocus hr=0x%08lX\r\n", (unsigned long)sink_hr);
            log_line(buf);
        }
        if (sink->lpVtbl && sink->lpVtbl->OnInitDocumentMgr && wrapped_focus) {
            HRESULT init_hr = sink->lpVtbl->OnInitDocumentMgr(sink, wrapped_focus);
            wsprintfA(buf, "MsctfShim: threadmgr sink OnInitDocumentMgr hr=0x%08lX\r\n",
                      (unsigned long)init_hr);
            log_line(buf);
        }
        if (sink->lpVtbl && sink->lpVtbl->OnPushContext && tm && tm->docmgr && tm->docmgr->top) {
            ITfContext *top = &tm->docmgr->top->iface;
            HRESULT push_hr = sink->lpVtbl->OnPushContext(sink, top);
            wsprintfA(buf, "MsctfShim: threadmgr sink OnPushContext hr=0x%08lX\r\n",
                      (unsigned long)push_hr);
            log_line(buf);
        }
        if (focus) focus->lpVtbl->Release(focus);
        (void)ctx;
    }
    if (riid && IsEqualIID(riid, &IID_LOCAL_ITfTextEditSink)) {
        notify_text_edit_sink_initial(s, punk);
    }
    /* ATOK advises ITfThreadFocusSink and waits to be told its thread has the
     * input focus. msctf would call OnSetThreadFocus; our shim must, or ATOK
     * concludes it is not focused/active and fails Activate. Deliver it now. */
    if (riid && IsEqualIID(riid, &IID_LOCAL_ITfThreadFocusSink)) {
        ITfThreadFocusSinkLocal *fs = (ITfThreadFocusSinkLocal *)punk;
        if (fs->lpVtbl && fs->lpVtbl->OnSetThreadFocus) {
            HRESULT hr = fs->lpVtbl->OnSetThreadFocus(fs);
            wsprintfA(buf, "MsctfShim: ThreadFocusSink OnSetThreadFocus hr=0x%08lX\r\n", (unsigned long)hr);
            log_line(buf);
        }
    }
    /* If ATOK advises the active-language-profile notify sink, tell it that its
     * profile just became the active one. */
    if (riid && IsEqualIID(riid, &IID_LOCAL_ITfActiveLangProfileNotifySink)) {
        ITfActiveLangSinkLocal *as = (ITfActiveLangSinkLocal *)punk;
        if (as->lpVtbl && as->lpVtbl->OnActivated) {
            HRESULT hr = as->lpVtbl->OnActivated(as, &CLSID_ATOK_TIP, &GUID_ATOK_PROFILE, TRUE);
            wsprintfA(buf, "MsctfShim: ActiveLangSink OnActivated hr=0x%08lX\r\n", (unsigned long)hr);
            log_line(buf);
        }
    }
    return S_OK;
}

static HRESULT WINAPI source_UnadviseSink(ITfSource *iface, DWORD dwCookie)
{
    MsctfSourceWrap *s = source_from_iface(iface);
    ULONG i;
    char buf[192];
    wsprintfA(buf, "MsctfShim: UnadviseSink cookie=%lu owner=%d count=%lu\r\n",
              (unsigned long)dwCookie, s->owner_kind, (unsigned long)s->sink_count);
    log_line(buf);
    for (i = 0; i < s->sink_count; i++) {
        if (s->sinks[i].cookie == dwCookie && s->sinks[i].sink) {
            wsprintfA(buf, "MsctfShim: UnadviseSink matched riid=%s sink=%p\r\n",
                      guid_string(&s->sinks[i].sink_iid), s->sinks[i].sink);
            log_line(buf);
            s->sinks[i].sink->lpVtbl->Release(s->sinks[i].sink);
            for (; i + 1 < s->sink_count; i++)
                s->sinks[i] = s->sinks[i + 1];
            s->sink_count--;
            break;
        }
    }
    if (s->cookie == dwCookie && s->sink) {
        s->sink->lpVtbl->Release(s->sink);
        s->sink = 0;
        s->cookie = 0;
    }
    return S_OK;
}

static const ITfSourceVtbl g_source_vtbl = {
    source_QI, source_AddRef, source_Release, source_AdviseSink, source_UnadviseSink
};

static inline MsctfSourceSingleWrap *source_single_from_iface(ITfSourceSingle *iface)
{
    return (MsctfSourceSingleWrap *)iface;
}

static HRESULT WINAPI source_single_QI(ITfSourceSingle *iface, REFIID riid, void **ppv)
{
    if (IsEqualIID(riid, &IID_IUnknown) || IsEqualIID(riid, &IID_LOCAL_ITfSourceSingle)) {
        *ppv = iface;
        iface->lpVtbl->AddRef(iface);
        return S_OK;
    }
    *ppv = 0;
    return E_NOINTERFACE;
}

static ULONG WINAPI source_single_AddRef(ITfSourceSingle *iface)
{
    return (ULONG)InterlockedIncrement(&source_single_from_iface(iface)->refs);
}

static ULONG WINAPI source_single_Release(ITfSourceSingle *iface)
{
    MsctfSourceSingleWrap *s = source_single_from_iface(iface);
    ULONG r = (ULONG)InterlockedDecrement(&s->refs);
    if (!r) {
        if (s->sink) s->sink->lpVtbl->Release(s->sink);
        HeapFree(GetProcessHeap(), 0, s);
    }
    return r;
}

static HRESULT WINAPI source_single_AdviseSingleSink(ITfSourceSingle *iface, TfClientId tid, REFIID riid, IUnknown *punk)
{
    MsctfSourceSingleWrap *s = source_single_from_iface(iface);
    char buf[192];
    if (!punk) return E_POINTER;
    if (s->sink) s->sink->lpVtbl->Release(s->sink);
    s->sink = punk;
    s->sink->lpVtbl->AddRef(s->sink);
    if (riid) s->sink_iid = *riid;
    s->tid = tid;
    wsprintfA(buf, "MsctfShim: AdviseSingleSink sink=%p tid=%lu riid=%s\r\n",
              punk, (unsigned long)tid, iid_name(riid) ? iid_name(riid) : "unknown");
    log_line(buf);
    if (riid) {
        wsprintfA(buf, "MsctfShim: AdviseSingleSink riid-guid=%s\r\n", guid_string(riid));
        log_line(buf);
    }
    if (riid && IsEqualIID(riid, &IID_LOCAL_ITfFunctionProvider)) {
        ITfFunctionProvider *prov = (ITfFunctionProvider *)punk;
        MsctfThreadMgrWrap *tm = (MsctfThreadMgrWrap *)s->owner;
        if (tm) {
            if (tm->advised_provider) tm->advised_provider->lpVtbl->Release(tm->advised_provider);
            tm->advised_provider = prov;
            tm->advised_provider->lpVtbl->AddRef(tm->advised_provider);
            log_ptr_hr("MsctfShim: FunctionProvider advised stored", tm, S_OK);
        }
        if (env_flag_enabled(L"AT_PROBE_FUNCTION_PROVIDER") && prov && prov->lpVtbl && prov->lpVtbl->GetType) {
            GUID type_guid;
            HRESULT type_hr;
            type_hr = prov->lpVtbl->GetType(prov, &type_guid);
            if (type_hr == S_OK) {
                wsprintfA(buf, "MsctfShim: FunctionProvider type=%08lX-%04X-%04X\r\n",
                          (unsigned long)type_guid.Data1, type_guid.Data2, type_guid.Data3);
                log_line(buf);
            }
            log_hr("MsctfShim: FunctionProvider GetType", type_hr);
        }
    }
    log_ptr2_hr("MsctfShim: AdviseSingleSink", iface, punk, S_OK);
    return S_OK;
}

static HRESULT WINAPI source_single_UnadviseSingleSink(ITfSourceSingle *iface, TfClientId tid, REFIID riid)
{
    MsctfSourceSingleWrap *s = source_single_from_iface(iface);
    char buf[192];
    (void)tid;
    (void)riid;
    if (s->sink) {
        s->sink->lpVtbl->Release(s->sink);
        s->sink = 0;
    }
    if (riid && IsEqualIID(riid, &IID_LOCAL_ITfFunctionProvider) && s->owner) {
        MsctfThreadMgrWrap *tm = (MsctfThreadMgrWrap *)s->owner;
        if (tm->advised_provider) {
            tm->advised_provider->lpVtbl->Release(tm->advised_provider);
            tm->advised_provider = 0;
            log_ptr_hr("MsctfShim: FunctionProvider advised cleared", tm, S_OK);
        }
    }
    wsprintfA(buf, "MsctfShim: UnadviseSingleSink tid=%lu riid=%s\r\n",
              (unsigned long)tid, iid_name(riid) ? iid_name(riid) : "unknown");
    log_line(buf);
    if (riid) {
        wsprintfA(buf, "MsctfShim: UnadviseSingleSink riid-guid=%s\r\n", guid_string(riid));
        log_line(buf);
    }
    log_ptr_hr("MsctfShim: UnadviseSingleSink", iface, S_OK);
    return S_OK;
}

static const ITfSourceSingleVtbl g_source_single_vtbl = {
    source_single_QI, source_single_AddRef, source_single_Release,
    source_single_AdviseSingleSink, source_single_UnadviseSingleSink
};

/* ITfCompartment */
static const char *variant_summary(const VARIANT *v)
{
    static char buf[4][128];
    static int idx;
    char *out = buf[idx++ & 3];

    if (!v) {
        wsprintfA(out, "null");
        return out;
    }

    switch (V_VT(v)) {
    case VT_EMPTY:
        wsprintfA(out, "VT_EMPTY");
        break;
    case VT_NULL:
        wsprintfA(out, "VT_NULL");
        break;
    case VT_I4:
        wsprintfA(out, "VT_I4=%ld", V_I4(v));
        break;
    case VT_UI4:
        wsprintfA(out, "VT_UI4=%lu", V_UI4(v));
        break;
    case VT_I2:
        wsprintfA(out, "VT_I2=%d", V_I2(v));
        break;
    case VT_UI2:
        wsprintfA(out, "VT_UI2=%u", V_UI2(v));
        break;
    case VT_BOOL:
        wsprintfA(out, "VT_BOOL=%d", V_BOOL(v));
        break;
    case VT_BSTR:
        wsprintfA(out, "VT_BSTR=%p", V_BSTR(v));
        break;
    case VT_UNKNOWN:
        wsprintfA(out, "VT_UNKNOWN=%p", V_UNKNOWN(v));
        break;
    case VT_DISPATCH:
        wsprintfA(out, "VT_DISPATCH=%p", V_DISPATCH(v));
        break;
    default:
        wsprintfA(out, "vt=0x%04X", V_VT(v));
        break;
    }
    return out;
}

static BOOL variant_is_shadowable(const VARIANT *v)
{
    if (!v) return FALSE;
    switch (V_VT(v)) {
    case VT_EMPTY:
    case VT_NULL:
    case VT_I4:
    case VT_UI4:
    case VT_I2:
    case VT_UI2:
    case VT_BOOL:
        return TRUE;
    default:
        return FALSE;
    }
}

static void compartment_shadow_set(REFGUID guid, const VARIANT *value)
{
    ULONG i;
    if (!guid || !variant_is_shadowable(value)) return;
    for (i = 0; i < g_compartment_value_count; i++) {
        if (g_compartment_values[i].valid && IsEqualGUID(&g_compartment_values[i].guid, guid)) {
            g_compartment_values[i].value = *value;
            return;
        }
    }
    if (g_compartment_value_count >= MSCTF_COMPARTMENT_VALUE_MAX) return;
    g_compartment_values[g_compartment_value_count].guid = *guid;
    g_compartment_values[g_compartment_value_count].value = *value;
    g_compartment_values[g_compartment_value_count].valid = TRUE;
    g_compartment_value_count++;
}

static BOOL compartment_shadow_get(REFGUID guid, VARIANT *value)
{
    ULONG i;
    if (!guid || !value) return FALSE;
    for (i = 0; i < g_compartment_value_count; i++) {
        if (g_compartment_values[i].valid && IsEqualGUID(&g_compartment_values[i].guid, guid)) {
            *value = g_compartment_values[i].value;
            return TRUE;
        }
    }
    if (IsEqualGUID(guid, &GUID_LOCAL_COMPARTMENT_KEYBOARD_OPENCLOSE)) {
        V_VT(value) = VT_I4;
        V_I4(value) = 1;
        return TRUE;
    }
    if (IsEqualGUID(guid, &GUID_LOCAL_COMPARTMENT_KEYBOARD_DISABLED) ||
        IsEqualGUID(guid, &GUID_LOCAL_COMPARTMENT_EMPTYCONTEXT)) {
        V_VT(value) = VT_I4;
        V_I4(value) = 0;
        return TRUE;
    }
    return FALSE;
}

static HRESULT WINAPI compartment_QI(ITfCompartment *iface, REFIID riid, void **ppv)
{
    MsctfCompartmentWrap *c = compartment_from_iface(iface);

    if (!ppv) return E_POINTER;
    if (IsEqualIID(riid, &IID_IUnknown) || IsEqualIID(riid, &IID_LOCAL_ITfCompartment)) {
        *ppv = iface;
        iface->lpVtbl->AddRef(iface);
        return S_OK;
    }
    if (IsEqualIID(riid, &IID_ITfSource)) {
        *ppv = &c->source_iface;
        iface->lpVtbl->AddRef(iface);
        log_ptr_hr("MsctfShim: Compartment QI ITfSource", iface, S_OK);
        return S_OK;
    }
    if (riid) {
        char buf[192];
        wsprintfA(buf, "MsctfShim: Compartment QI fallback %s guid=%s\r\n",
                  iid_name(riid) ? iid_name(riid) : guid_string(riid),
                  guid_string(&c->guid));
        log_line(buf);
    }
    if (!c->orig) {
        *ppv = 0;
        return E_NOINTERFACE;
    }
    return c->orig->lpVtbl->QueryInterface(c->orig, riid, ppv);
}

static ULONG WINAPI compartment_AddRef(ITfCompartment *iface)
{
    MsctfCompartmentWrap *c = compartment_from_iface(iface);
    return (ULONG)InterlockedIncrement(&c->refs);
}

static ULONG WINAPI compartment_Release(ITfCompartment *iface)
{
    MsctfCompartmentWrap *c = compartment_from_iface(iface);
    ULONG refs = (ULONG)InterlockedDecrement(&c->refs);
    if (!refs) {
        if (c->sink) c->sink->lpVtbl->Release(c->sink);
        if (c->orig) c->orig->lpVtbl->Release(c->orig);
        HeapFree(GetProcessHeap(), 0, c);
    }
    return refs;
}

static HRESULT WINAPI compartment_SetValue(ITfCompartment *iface, TfClientId tid, const VARIANT *pvarValue)
{
    MsctfCompartmentWrap *c = compartment_from_iface(iface);
    HRESULT hr;
    char buf[256];

    wsprintfA(buf, "MsctfShim: Compartment SetValue guid=%s tid=%lu value=%s\r\n",
              guid_string(&c->guid), (unsigned long)tid, variant_summary(pvarValue));
    log_line(buf);
    if (!c->orig) return E_NOINTERFACE;
    hr = c->orig->lpVtbl->SetValue(c->orig, tid, pvarValue);
    if (hr == S_OK)
        compartment_shadow_set(&c->guid, pvarValue);
    log_ptr_hr("MsctfShim: Compartment SetValue", iface, hr);
    return hr;
}

static HRESULT WINAPI compartment_GetValue(ITfCompartment *iface, VARIANT *pvarValue)
{
    MsctfCompartmentWrap *c = compartment_from_iface(iface);
    HRESULT hr;
    char buf[256];

    if (!pvarValue) return E_POINTER;
    if (!c->orig) return E_NOINTERFACE;
    hr = c->orig->lpVtbl->GetValue(c->orig, pvarValue);
    if (hr != S_OK && compartment_shadow_get(&c->guid, pvarValue))
        hr = S_OK;
    wsprintfA(buf, "MsctfShim: Compartment GetValue guid=%s hr=0x%08lX value=%s\r\n",
              guid_string(&c->guid), (unsigned long)hr, variant_summary(pvarValue));
    log_line(buf);
    return hr;
}

static const ITfCompartmentVtbl g_compartment_vtbl = {
    compartment_QI, compartment_AddRef, compartment_Release,
    compartment_SetValue, compartment_GetValue
};

static HRESULT WINAPI compartment_source_QI(ITfSource *iface, REFIID riid, void **ppv)
{
    MsctfCompartmentWrap *c = compartment_from_source_iface(iface);

    if (!ppv) return E_POINTER;
    if (IsEqualIID(riid, &IID_ITfSource)) {
        *ppv = iface;
        iface->lpVtbl->AddRef(iface);
        return S_OK;
    }
    return c->iface.lpVtbl->QueryInterface(&c->iface, riid, ppv);
}

static ULONG WINAPI compartment_source_AddRef(ITfSource *iface)
{
    MsctfCompartmentWrap *c = compartment_from_source_iface(iface);
    return c->iface.lpVtbl->AddRef(&c->iface);
}

static ULONG WINAPI compartment_source_Release(ITfSource *iface)
{
    MsctfCompartmentWrap *c = compartment_from_source_iface(iface);
    return c->iface.lpVtbl->Release(&c->iface);
}

static HRESULT WINAPI compartment_source_AdviseSink(ITfSource *iface, REFIID riid, IUnknown *punk, DWORD *pdwCookie)
{
    MsctfCompartmentWrap *c = compartment_from_source_iface(iface);
    char buf[256];

    if (!punk || !pdwCookie) return E_POINTER;
    if (c->sink) {
        c->sink->lpVtbl->Release(c->sink);
        c->sink = 0;
    }
    c->sink = punk;
    c->sink->lpVtbl->AddRef(c->sink);
    c->sink_iid = riid ? *riid : IID_IUnknown;
    if (!c->next_cookie) c->next_cookie = 1;
    c->cookie = c->next_cookie++;
    *pdwCookie = c->cookie;

    wsprintfA(buf, "MsctfShim: Compartment Source AdviseSink guid=%s riid=%s cookie=%lu\r\n",
              guid_string(&c->guid),
              riid ? (iid_name(riid) ? iid_name(riid) : guid_string(riid)) : "null",
              (unsigned long)c->cookie);
    log_line(buf);
    return S_OK;
}

static HRESULT WINAPI compartment_source_UnadviseSink(ITfSource *iface, DWORD dwCookie)
{
    MsctfCompartmentWrap *c = compartment_from_source_iface(iface);
    char buf[192];

    wsprintfA(buf, "MsctfShim: Compartment Source UnadviseSink guid=%s cookie=%lu\r\n",
              guid_string(&c->guid), (unsigned long)dwCookie);
    log_line(buf);
    if (c->sink && (!dwCookie || dwCookie == c->cookie)) {
        c->sink->lpVtbl->Release(c->sink);
        c->sink = 0;
        c->cookie = 0;
    }
    return S_OK;
}

static const ITfSourceVtbl g_compartment_source_vtbl = {
    compartment_source_QI, compartment_source_AddRef, compartment_source_Release,
    compartment_source_AdviseSink, compartment_source_UnadviseSink
};

static ITfCompartment *make_compartment(ITfCompartment *orig, REFGUID guid)
{
    MsctfCompartmentWrap *c;

    if (!orig || !guid) return 0;
    c = (MsctfCompartmentWrap *)HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, sizeof(*c));
    if (!c) return 0;
    c->iface.lpVtbl = &g_compartment_vtbl;
    c->source_iface.lpVtbl = &g_compartment_source_vtbl;
    c->refs = 1;
    c->orig = orig;
    c->guid = *guid;
    c->orig->lpVtbl->AddRef(c->orig);
    return &c->iface;
}

/* ITfCompartmentMgr */
static HRESULT WINAPI compartmentmgr_QI(ITfCompartmentMgr *iface, REFIID riid, void **ppv)
{
    MsctfCompartmentMgrWrap *m = compartmentmgr_from_iface(iface);
    if (IsEqualIID(riid, &IID_IUnknown) || IsEqualIID(riid, &IID_LOCAL_ITfCompartmentMgr)) {
        *ppv = iface;
        iface->lpVtbl->AddRef(iface);
        return S_OK;
    }
    if (riid) {
        char buf[192];
        wsprintfA(buf, "MsctfShim: CompartmentMgr QI fallback %s %s\r\n",
                  iid_name(riid) ? iid_name(riid) : "unknown", guid_string(riid));
        log_line(buf);
    }
    if (!m->orig) {
        *ppv = 0;
        return E_NOINTERFACE;
    }
    return m->orig->lpVtbl->QueryInterface(m->orig, riid, ppv);
}

static ULONG WINAPI compartmentmgr_AddRef(ITfCompartmentMgr *iface)
{
    MsctfCompartmentMgrWrap *m = compartmentmgr_from_iface(iface);
    return (ULONG)InterlockedIncrement(&m->refs);
}

static ULONG WINAPI compartmentmgr_Release(ITfCompartmentMgr *iface)
{
    MsctfCompartmentMgrWrap *m = compartmentmgr_from_iface(iface);
    ULONG refs = (ULONG)InterlockedDecrement(&m->refs);
    if (!refs) {
        if (m->orig) m->orig->lpVtbl->Release(m->orig);
        HeapFree(GetProcessHeap(), 0, m);
    }
    return refs;
}

static HRESULT WINAPI compartmentmgr_GetCompartment(ITfCompartmentMgr *iface, REFGUID rguid, ITfCompartment **ppcomp)
{
    MsctfCompartmentMgrWrap *m = compartmentmgr_from_iface(iface);
    ITfCompartment *orig_comp = 0;
    ITfCompartment *wrapped_comp;
    char buf[192];
    HRESULT hr;
    if (!ppcomp) return E_POINTER;
    *ppcomp = 0;
    if (rguid) {
        wsprintfA(buf, "MsctfShim: CompartmentMgr GetCompartment guid=%s\r\n", guid_string(rguid));
        log_line(buf);
    }
    if (!m->orig) return E_NOINTERFACE;
    hr = m->orig->lpVtbl->GetCompartment(m->orig, rguid, &orig_comp);
    log_ptr_hr("MsctfShim: CompartmentMgr GetCompartment", iface, hr);
    if (hr != S_OK || !orig_comp) return hr;
    wrapped_comp = make_compartment(orig_comp, rguid);
    if (!wrapped_comp) {
        orig_comp->lpVtbl->Release(orig_comp);
        return E_OUTOFMEMORY;
    }
    *ppcomp = wrapped_comp;
    log_ptr2_hr("MsctfShim: CompartmentMgr GetCompartment wrapped", orig_comp, *ppcomp, S_OK);
    orig_comp->lpVtbl->Release(orig_comp);
    return hr;
}

static HRESULT WINAPI compartmentmgr_ClearCompartment(ITfCompartmentMgr *iface, TfClientId tid, REFGUID rguid)
{
    MsctfCompartmentMgrWrap *m = compartmentmgr_from_iface(iface);
    char buf[192];
    HRESULT hr;
    if (rguid) {
        wsprintfA(buf, "MsctfShim: CompartmentMgr ClearCompartment tid=%lu guid=%s\r\n",
                  (unsigned long)tid, guid_string(rguid));
        log_line(buf);
    }
    if (!m->orig) return E_NOINTERFACE;
    hr = m->orig->lpVtbl->ClearCompartment(m->orig, tid, rguid);
    log_ptr_hr("MsctfShim: CompartmentMgr ClearCompartment", iface, hr);
    return hr;
}

static HRESULT WINAPI compartmentmgr_EnumCompartments(ITfCompartmentMgr *iface, IEnumGUID **ppEnum)
{
    MsctfCompartmentMgrWrap *m = compartmentmgr_from_iface(iface);
    HRESULT hr;
    if (!ppEnum) return E_POINTER;
    if (!m->orig) return E_NOINTERFACE;
    hr = m->orig->lpVtbl->EnumCompartments(m->orig, ppEnum);
    log_ptr_hr("MsctfShim: CompartmentMgr EnumCompartments", iface, hr);
    return hr;
}

static const ITfCompartmentMgrVtbl g_compartmentmgr_vtbl = {
    compartmentmgr_QI, compartmentmgr_AddRef, compartmentmgr_Release,
    compartmentmgr_GetCompartment, compartmentmgr_ClearCompartment, compartmentmgr_EnumCompartments
};

static MsctfCompartmentMgrWrap *make_compartmentmgr(ITfCompartmentMgr *orig, void *owner)
{
    MsctfCompartmentMgrWrap *m = (MsctfCompartmentMgrWrap *)HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, sizeof(*m));
    if (!m) return 0;
    m->iface.lpVtbl = &g_compartmentmgr_vtbl;
    m->refs = 1;
    m->orig = orig;
    m->owner = owner;
    if (m->orig) m->orig->lpVtbl->AddRef(m->orig);
    return m;
}

static HRESULT wrap_compartment_mgr_result(ITfCompartmentMgr *orig, void *owner, void **ppv, const char *label)
{
    MsctfCompartmentMgrWrap *wrap;
    if (!orig || !ppv) return E_POINTER;
    wrap = make_compartmentmgr(orig, owner);
    if (!wrap) {
        orig->lpVtbl->Release(orig);
        return E_OUTOFMEMORY;
    }
    *ppv = &wrap->iface;
    log_ptr2_hr(label, orig, *ppv, S_OK);
    orig->lpVtbl->Release(orig);
    return S_OK;
}

/* ITfUIElementMgr */
static HRESULT WINAPI uielementmgr_QI(ITfUIElementMgr *iface, REFIID riid, void **ppv)
{
    MsctfUIElementMgrWrap *m = uielementmgr_from_iface(iface);

    if (!ppv) return E_POINTER;
    if (IsEqualIID(riid, &IID_IUnknown) || IsEqualIID(riid, &IID_LOCAL_ITfUIElementMgr)) {
        *ppv = iface;
        iface->lpVtbl->AddRef(iface);
        return S_OK;
    }
    if (!m->orig) {
        *ppv = 0;
        return E_NOINTERFACE;
    }
    return m->orig->lpVtbl->QueryInterface(m->orig, riid, ppv);
}

static ULONG WINAPI uielementmgr_AddRef(ITfUIElementMgr *iface)
{
    return (ULONG)InterlockedIncrement(&uielementmgr_from_iface(iface)->refs);
}

static ULONG WINAPI uielementmgr_Release(ITfUIElementMgr *iface)
{
    MsctfUIElementMgrWrap *m = uielementmgr_from_iface(iface);
    ULONG refs = (ULONG)InterlockedDecrement(&m->refs);
    if (!refs) {
        if (m->cached_element) m->cached_element->lpVtbl->Release(m->cached_element);
        if (m->orig) m->orig->lpVtbl->Release(m->orig);
        HeapFree(GetProcessHeap(), 0, m);
    }
    return refs;
}

/* Log a wide string as UTF-8 (so Japanese candidates are readable in the log),
 * falling back to the U+ codepoint preview if conversion fails/overflows. */
static void log_utf8_field(const char *tag, const WCHAR *text, int cch)
{
    char buf[600];
    int taglen, w;
    if (!text) {
        wsprintfA(buf, "%s=<null>\r\n", tag);
        log_line(buf);
        return;
    }
    if (cch < 0) cch = lstrlenW(text);
    taglen = wsprintfA(buf, "%s=", tag);
    w = WideCharToMultiByte(CP_UTF8, 0, text, cch, buf + taglen,
                            (int)sizeof(buf) - taglen - 4, NULL, NULL);
    if (w <= 0) {
        log_wchars_preview(tag, text, cch);
        return;
    }
    buf[taglen + w] = '\r';
    buf[taglen + w + 1] = '\n';
    buf[taglen + w + 2] = 0;
    log_line(buf);
}

/*
 * Introspect a TSF UI element ATOK hands us. If it is an
 * ITfCandidateListUIElement (the henkan candidate window), dump the candidate
 * list — count, current selection/page, and each candidate string. This is the
 * read surface for modore-style candidate extraction. Non-candidate elements
 * (reading info, ATOK's own UI) are logged but otherwise ignored. Read-only:
 * safe to call on every Begin/Update without affecting conversion.
 */
/* --- private candidate-UI-element object dump (AT_DUMP_UIELEM) -----------
 * AT's candidate UI element is a private type (no ITfCandidateListUIElement),
 * so the candidate strings + 使い分け comment are not exposed via TSF. Crack it
 * open: BFS the object's pointer graph (heap-only pointers, deduped, depth/node
 * capped) and dump each node hex+UTF-16, so candidate strings / comment text in
 * the backing candidate_view_model / ExternalView model become visible. */
static BOOL mem_readable_(const void *p, SIZE_T len, BOOL want_private)
{
    MEMORY_BASIC_INFORMATION mbi;
    DWORD r = PAGE_READONLY | PAGE_READWRITE | PAGE_WRITECOPY |
              PAGE_EXECUTE_READ | PAGE_EXECUTE_READWRITE | PAGE_EXECUTE_WRITECOPY;
    if (!p) return FALSE;
    if (VirtualQuery(p, &mbi, sizeof(mbi)) != sizeof(mbi)) return FALSE;
    if (mbi.State != MEM_COMMIT) return FALSE;
    if (!(mbi.Protect & r) || (mbi.Protect & (PAGE_GUARD | PAGE_NOACCESS))) return FALSE;
    if (want_private && mbi.Type != MEM_PRIVATE) return FALSE;
    if ((const BYTE *)p + len > (const BYTE *)mbi.BaseAddress + mbi.RegionSize) return FALSE;
    return TRUE;
}

static void dump_mem_rows(const BYTE *base, int len, const char *prefix)
{
    int off;
    for (off = 0; off < len; off += 16) {
        char line[256], txt[80];
        int li = 0, ti = 0, k;
        li += wsprintfA(line + li, "%s+%03X ", prefix, off);
        for (k = 0; k < 16; k++)
            li += (off + k < len) ? wsprintfA(line + li, "%02X ", base[off + k])
                                  : wsprintfA(line + li, "   ");
        for (k = 0; k + 1 < 16 && off + k + 1 < len; k += 2) {
            WCHAR w = (WCHAR)(base[off + k] | (base[off + k + 1] << 8));
            char u[8];
            int n = (w >= 0x20 && w != 0x7f)
                        ? WideCharToMultiByte(CP_UTF8, 0, &w, 1, u, sizeof(u) - 1, 0, 0)
                        : 0;
            if (n > 0) { u[n] = 0; ti += wsprintfA(txt + ti, "%s", u); }
            else txt[ti++] = '.';
        }
        txt[ti] = 0;
        wsprintfA(line + li, " | %s\r\n", txt);
        log_line(line);
    }
}

static void dump_object_graph(void *root, const char *tag)
{
    UINT_PTR visited[160];
    struct { UINT_PTR a; int d; } q[256];
    int nvis = 0, qh = 0, qt = 0, dumps = 0;
    int node_sz = (int)get_env_dword(L"AT_UIELEM_SZ", 0x60);
    int max_depth = (int)get_env_dword(L"AT_UIELEM_DEPTH", 3);
    int max_nodes = (int)get_env_dword(L"AT_UIELEM_NODES", 90);
    char buf[160];
    if (!root) return;
    if (node_sz > 0x100) node_sz = 0x100;
    wsprintfA(buf, "MsctfShim: UIELEM graph [%s] root=%p depth=%d nodes=%d\r\n",
              tag, root, max_depth, max_nodes);
    log_line(buf);
    q[qt].a = (UINT_PTR)root; q[qt].d = 0; qt++;
    while (qh < qt && dumps < max_nodes) {
        UINT_PTR a = q[qh].a;
        int d = q[qh].d, i, seen = 0, s;
        qh++;
        for (i = 0; i < nvis; i++) if (visited[i] == a) { seen = 1; break; }
        if (seen) continue;
        if (nvis < 160) visited[nvis++] = a;
        if (!mem_readable_((void *)a, (SIZE_T)node_sz, FALSE)) continue;
        wsprintfA(buf, "  [d%d] %08lX\r\n", d, (unsigned long)a);
        log_line(buf);
        dump_mem_rows((const BYTE *)a, node_sz, "     ");
        dumps++;
        if (d < max_depth) {
            const BYTE *p = (const BYTE *)a;
            for (s = 0; s + 4 <= node_sz; s += 4) {
                UINT_PTR v = *(const UINT_PTR *)(p + s);
                if (v >= 0x00010000 && v < 0x7FFE0000 && (v & 3) == 0 &&
                    qt < 256 && mem_readable_((void *)v, 4, TRUE)) {
                    q[qt].a = v; q[qt].d = d + 1; qt++;
                }
            }
        }
    }
    wsprintfA(buf, "MsctfShim: UIELEM graph [%s] done: %d node(s)\r\n", tag, dumps);
    log_line(buf);
}

static void dump_candidate_ui_element(ITfUIElement *element, const char *when)
{
    if (element && env_flag_enabled(L"AT_DUMP_UIELEM")) {
        static int graph_dumps = 0;
        int cap = (int)get_env_dword(L"AT_UIELEM_TIMES", 3);
        if (graph_dumps < cap) {
            graph_dumps++;
            dump_object_graph(element, when);
        }
    }
    ITfCandidateListUIElementLocal *cand = 0;
    HRESULT hr;
    UINT count = 0, sel = (UINT)-1, page = 0, i;
    char buf[256];

    if (!element) return;
    ensure_oleaut32();

    hr = element->lpVtbl->QueryInterface(element,
                                         &IID_LOCAL_ITfCandidateListUIElement,
                                         (void **)&cand);
    if (hr != S_OK || !cand) {
        /* Not a candidate list. Identify what it IS via the base ITfUIElement
         * (GetGUID names the element kind: candidate vs reading vs tooltip vs
         * ATOK-private), so we know which surface actually carries candidates. */
        GUID g;
        BSTR desc = 0;
        HRESULT hg = element->lpVtbl->GetGUID(element, &g);
        HRESULT hd = element->lpVtbl->GetDescription(element, &desc);
        wsprintfA(buf, "MsctfShim: CANDIDATES %s element=%p not-a-candidate-list candHr=0x%08lX guid=%s(0x%08lX)\r\n",
                  when, element, (unsigned long)hr,
                  hg == S_OK ? guid_string(&g) : "?", (unsigned long)hg);
        log_line(buf);
        if (hd == S_OK && desc) {
            log_utf8_field("MsctfShim:   element desc", desc, -1);
            if (pSysFreeString) pSysFreeString(desc);
        }
        return;
    }

    cand->lpVtbl->GetCount(cand, &count);
    cand->lpVtbl->GetSelection(cand, &sel);
    cand->lpVtbl->GetCurrentPage(cand, &page);
    wsprintfA(buf, "MsctfShim: CANDIDATES %s count=%u selection=%d page=%u\r\n",
              when, count, (int)sel, page);
    log_line(buf);

    for (i = 0; i < count && i < 64; i++) {
        BSTR s = 0;
        hr = cand->lpVtbl->GetString(cand, i, &s);
        if (hr == S_OK && s) {
            char tag[56];
            wsprintfA(tag, "MsctfShim:   cand[%u]%s", i, (i == sel) ? "*" : "");
            log_utf8_field(tag, s, -1);
            if (pSysFreeString) pSysFreeString(s);
        } else {
            wsprintfA(buf, "MsctfShim:   cand[%u] GetString hr=0x%08lX\r\n",
                      i, (unsigned long)hr);
            log_line(buf);
        }
    }
    cand->lpVtbl->Release(cand);
}

static HRESULT WINAPI uielementmgr_BeginUIElement(ITfUIElementMgr *iface, ITfUIElement *element, BOOL *show, DWORD *id)
{
    MsctfUIElementMgrWrap *m = uielementmgr_from_iface(iface);
    HRESULT hr = S_OK;
    char buf[192];

    if (!m->orig) {
        if (id && !*id) *id = m->cached_id ? m->cached_id + 1 : 1;
        if (show) *show = TRUE;
        wsprintfA(buf, "MsctfShim: UIElementMgr BeginUIElement dummy element=%p show=%d id=%lu\r\n",
                  element, show ? *show : 0, id ? (unsigned long)*id : 0);
        log_line(buf);
    } else {
        hr = m->orig->lpVtbl->BeginUIElement(m->orig, element, show, id);
    }
    wsprintfA(buf, "MsctfShim: UIElementMgr BeginUIElement element=%p show=%d id=%lu hr=0x%08lX\r\n",
              element, show ? *show : 0, id ? (unsigned long)*id : 0, (unsigned long)hr);
    log_line(buf);
    if (hr == E_NOTIMPL) {
        if (id && !*id) *id = 1;
        if (show) *show = TRUE;
        log_line("MsctfShim: UIElementMgr BeginUIElement smoothing E_NOTIMPL to S_OK\r\n");
        hr = S_OK;
    }
    if (hr == S_OK && element && id) {
        if (m->cached_element) m->cached_element->lpVtbl->Release(m->cached_element);
        m->cached_element = element;
        m->cached_element->lpVtbl->AddRef(m->cached_element);
        m->cached_id = *id;
        wsprintfA(buf, "MsctfShim: UIElementMgr cached id=%lu element=%p\r\n",
                  (unsigned long)m->cached_id, m->cached_element);
        log_line(buf);
    }
    if (hr == S_OK && element) {
        dump_candidate_ui_element(element, "BeginUIElement");
    }
    /* UI-less candidate mode (opt-in): returning show=FALSE tells ATOK the app
     * draws the candidate UI, so it populates the candidate list element fully
     * for us instead of relying on its own (headless) window. */
    if (show && env_flag_enabled(L"AT_UILESS_CANDIDATES")) {
        *show = FALSE;
    }
    return hr;
}

static HRESULT WINAPI uielementmgr_UpdateUIElement(ITfUIElementMgr *iface, DWORD id)
{
    MsctfUIElementMgrWrap *m = uielementmgr_from_iface(iface);
    HRESULT hr;
    char buf[160];

    if (!m->orig) {
        wsprintfA(buf, "MsctfShim: UIElementMgr UpdateUIElement dummy id=%lu\r\n",
                  (unsigned long)id);
        log_line(buf);
        return S_OK;
    }
    hr = m->orig->lpVtbl->UpdateUIElement(m->orig, id);
    wsprintfA(buf, "MsctfShim: UIElementMgr UpdateUIElement id=%lu hr=0x%08lX\r\n",
              (unsigned long)id, (unsigned long)hr);
    log_line(buf);
    if (m->cached_element && id == m->cached_id) {
        dump_candidate_ui_element(m->cached_element, "UpdateUIElement");
    }
    return hr;
}

static HRESULT WINAPI uielementmgr_EndUIElement(ITfUIElementMgr *iface, DWORD id)
{
    MsctfUIElementMgrWrap *m = uielementmgr_from_iface(iface);
    HRESULT hr;
    char buf[160];

    if (!m->orig) {
        wsprintfA(buf, "MsctfShim: UIElementMgr EndUIElement dummy id=%lu\r\n",
                  (unsigned long)id);
        log_line(buf);
        return S_OK;
    }
    hr = m->orig->lpVtbl->EndUIElement(m->orig, id);
    wsprintfA(buf, "MsctfShim: UIElementMgr EndUIElement id=%lu hr=0x%08lX\r\n",
              (unsigned long)id, (unsigned long)hr);
    log_line(buf);
    if (hr == E_NOTIMPL) {
        log_line("MsctfShim: UIElementMgr EndUIElement smoothing E_NOTIMPL to S_OK\r\n");
        return S_OK;
    }
    return hr;
}

static HRESULT WINAPI uielementmgr_GetUIElement(ITfUIElementMgr *iface, DWORD id, ITfUIElement **element)
{
    MsctfUIElementMgrWrap *m = uielementmgr_from_iface(iface);
    HRESULT hr;
    char buf[176];

    if (!m->orig) {
        if (element && m->cached_element && id == m->cached_id) {
            *element = m->cached_element;
            (*element)->lpVtbl->AddRef(*element);
            wsprintfA(buf, "MsctfShim: UIElementMgr GetUIElement dummy cached id=%lu element=%p\r\n",
                      (unsigned long)id, *element);
            log_line(buf);
            return S_OK;
        }
        if (element) *element = 0;
        wsprintfA(buf, "MsctfShim: UIElementMgr GetUIElement dummy missing id=%lu\r\n",
                  (unsigned long)id);
        log_line(buf);
        return E_INVALIDARG;
    }
    hr = m->orig->lpVtbl->GetUIElement(m->orig, id, element);
    wsprintfA(buf, "MsctfShim: UIElementMgr GetUIElement id=%lu element=%p hr=0x%08lX\r\n",
              (unsigned long)id, element ? *element : 0, (unsigned long)hr);
    log_line(buf);
    if (hr != S_OK && element && m->cached_element && id == m->cached_id) {
        *element = m->cached_element;
        (*element)->lpVtbl->AddRef(*element);
        wsprintfA(buf, "MsctfShim: UIElementMgr GetUIElement cached id=%lu element=%p\r\n",
                  (unsigned long)id, *element);
        log_line(buf);
        return S_OK;
    }
    return hr;
}

static HRESULT WINAPI uielementmgr_EnumUIElements(ITfUIElementMgr *iface, IEnumTfUIElements **enum_elements)
{
    MsctfUIElementMgrWrap *m = uielementmgr_from_iface(iface);
    HRESULT hr;

    if (!m->orig) {
        if (enum_elements) *enum_elements = 0;
        log_ptr_hr("MsctfShim: UIElementMgr EnumUIElements dummy", iface, E_NOTIMPL);
        return E_NOTIMPL;
    }
    hr = m->orig->lpVtbl->EnumUIElements(m->orig, enum_elements);
    log_ptr_hr("MsctfShim: UIElementMgr EnumUIElements", iface, hr);
    return hr;
}

static const ITfUIElementMgrVtbl g_uielementmgr_vtbl = {
    uielementmgr_QI, uielementmgr_AddRef, uielementmgr_Release,
    uielementmgr_BeginUIElement, uielementmgr_UpdateUIElement, uielementmgr_EndUIElement,
    uielementmgr_GetUIElement, uielementmgr_EnumUIElements
};

static HRESULT wrap_uielementmgr_result(ITfUIElementMgr *orig, void **ppv, const char *label)
{
    MsctfUIElementMgrWrap *wrap;

    if (!orig || !ppv) return E_POINTER;
    wrap = (MsctfUIElementMgrWrap *)HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, sizeof(*wrap));
    if (!wrap) {
        orig->lpVtbl->Release(orig);
        return E_OUTOFMEMORY;
    }
    wrap->iface.lpVtbl = &g_uielementmgr_vtbl;
    wrap->refs = 1;
    if (env_flag_enabled(L"AT_DUMMY_UIELEMENT_MGR")) {
        log_line("MsctfShim: UIElementMgr using standalone dummy\r\n");
        wrap->orig = 0;
    } else {
        wrap->orig = orig;
        wrap->orig->lpVtbl->AddRef(wrap->orig);
    }
    *ppv = &wrap->iface;
    log_ptr2_hr(label, orig, *ppv, S_OK);
    orig->lpVtbl->Release(orig);
    return S_OK;
}

static inline MsctfLangBarItemEnum *langbaritemenum_from_iface(IEnumTfLangBarItems *iface)
{
    return (MsctfLangBarItemEnum *)iface;
}

static MsctfLangBarItemEnum *langbar_make_item_enum(MsctfLangBarItemMgrWrap *mgr);

static void langbar_notify_sinks(MsctfLangBarItemMgrWrap *m, DWORD flags)
{
    ULONG i;

    for (i = 0; i < m->sink_count; i++) {
        ITfLangBarItemSink *sink = m->sinks[i].sink;
        if (sink && sink->lpVtbl && sink->lpVtbl->OnUpdate)
            sink->lpVtbl->OnUpdate(sink, flags);
    }
}

static ITfLangBarItem *langbar_find_item_by_guid(MsctfLangBarItemMgrWrap *m, REFGUID rguid)
{
    ULONG i;

    if (!rguid) return 0;
    for (i = 0; i < m->item_count; i++) {
        if (IsEqualGUID(&m->items[i].guid, rguid))
            return m->items[i].item;
    }
    return 0;
}

static int langbar_find_item_index_by_ptr(MsctfLangBarItemMgrWrap *m, ITfLangBarItem *punk)
{
    ULONG i;

    if (!punk) return -1;
    for (i = 0; i < m->item_count; i++) {
        if (m->items[i].item == punk)
            return (int)i;
    }
    return -1;
}

static int langbar_find_item_index_by_guid(MsctfLangBarItemMgrWrap *m, REFGUID rguid)
{
    ULONG i;

    if (!rguid) return -1;
    for (i = 0; i < m->item_count; i++) {
        if (IsEqualGUID(&m->items[i].guid, rguid))
            return (int)i;
    }
    return -1;
}

static void langbar_remove_item_at(MsctfLangBarItemMgrWrap *m, ULONG index)
{
    ULONG i;

    if (index >= m->item_count) return;
    if (m->items[index].item)
        m->items[index].item->lpVtbl->Release(m->items[index].item);
    for (i = index; i + 1 < m->item_count; i++)
        m->items[i] = m->items[i + 1];
    m->item_count--;
}

static HRESULT WINAPI langbaritemenum_QI(IEnumTfLangBarItems *iface, REFIID riid, void **ppv)
{
    if (IsEqualIID(riid, &IID_IUnknown) || IsEqualIID(riid, &IID_LOCAL_IEnumTfLangBarItems)) {
        *ppv = iface;
        iface->lpVtbl->AddRef(iface);
        return S_OK;
    }
    *ppv = 0;
    return E_NOINTERFACE;
}

static ULONG WINAPI langbaritemenum_AddRef(IEnumTfLangBarItems *iface)
{
    return (ULONG)InterlockedIncrement(&langbaritemenum_from_iface(iface)->refs);
}

static ULONG WINAPI langbaritemenum_Release(IEnumTfLangBarItems *iface)
{
    MsctfLangBarItemEnum *e = langbaritemenum_from_iface(iface);
    ULONG refs = (ULONG)InterlockedDecrement(&e->refs);

    if (!refs) {
        if (e->mgr) e->mgr->iface.lpVtbl->Release(&e->mgr->iface);
        HeapFree(GetProcessHeap(), 0, e);
    }
    return refs;
}

static HRESULT WINAPI langbaritemenum_Clone(IEnumTfLangBarItems *iface, IEnumTfLangBarItems **ppEnum)
{
    MsctfLangBarItemEnum *src = langbaritemenum_from_iface(iface);
    MsctfLangBarItemEnum *dst;

    if (!ppEnum) return E_POINTER;
    dst = langbar_make_item_enum(src->mgr);
    if (!dst) return E_OUTOFMEMORY;
    dst->index = src->index;
    *ppEnum = &dst->iface;
    return S_OK;
}

static HRESULT WINAPI langbaritemenum_Next(IEnumTfLangBarItems *iface, ULONG ulCount, ITfLangBarItem **ppItem, ULONG *pcFetched)
{
    MsctfLangBarItemEnum *e = langbaritemenum_from_iface(iface);
    ULONG fetched = 0;

    if (pcFetched) *pcFetched = 0;
    if (!ppItem || !ulCount) return E_INVALIDARG;
    while (fetched < ulCount && e->index < e->mgr->item_count) {
        ITfLangBarItem *item = e->mgr->items[e->index].item;
        item->lpVtbl->AddRef(item);
        ppItem[fetched++] = item;
        e->index++;
    }
    if (pcFetched) *pcFetched = fetched;
    return fetched ? S_OK : S_FALSE;
}

static HRESULT WINAPI langbaritemenum_Reset(IEnumTfLangBarItems *iface)
{
    langbaritemenum_from_iface(iface)->index = 0;
    return S_OK;
}

static HRESULT WINAPI langbaritemenum_Skip(IEnumTfLangBarItems *iface, ULONG ulCount)
{
    MsctfLangBarItemEnum *e = langbaritemenum_from_iface(iface);

    if (e->index + ulCount > e->mgr->item_count)
        return S_FALSE;
    e->index += ulCount;
    return S_OK;
}

static const IEnumTfLangBarItemsVtbl g_langbaritemenum_vtbl = {
    langbaritemenum_QI, langbaritemenum_AddRef, langbaritemenum_Release,
    langbaritemenum_Clone, langbaritemenum_Next, langbaritemenum_Reset, langbaritemenum_Skip
};

static MsctfLangBarItemEnum *langbar_make_item_enum(MsctfLangBarItemMgrWrap *mgr)
{
    MsctfLangBarItemEnum *e;

    e = (MsctfLangBarItemEnum *)HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, sizeof(*e));
    if (!e) return 0;
    e->iface.lpVtbl = &g_langbaritemenum_vtbl;
    e->refs = 1;
    e->mgr = mgr;
    e->index = 0;
    mgr->iface.lpVtbl->AddRef(&mgr->iface);
    return e;
}

static HRESULT WINAPI langbaritemmgr_QI(ITfLangBarItemMgr *iface, REFIID riid, void **ppv)
{
    MsctfLangBarItemMgrWrap *m = langbaritemmgr_from_iface(iface);
    if (IsEqualIID(riid, &IID_IUnknown) || IsEqualIID(riid, &IID_LOCAL_ITfLangBarItemMgr)) {
        *ppv = iface;
        iface->lpVtbl->AddRef(iface);
        return S_OK;
    }
    if (!m->orig) {
        *ppv = 0;
        return E_NOINTERFACE;
    }
    return m->orig->lpVtbl->QueryInterface(m->orig, riid, ppv);
}

static ULONG WINAPI langbaritemmgr_AddRef(ITfLangBarItemMgr *iface)
{
    return (ULONG)InterlockedIncrement(&langbaritemmgr_from_iface(iface)->refs);
}

static ULONG WINAPI langbaritemmgr_Release(ITfLangBarItemMgr *iface)
{
    MsctfLangBarItemMgrWrap *m = langbaritemmgr_from_iface(iface);
    ULONG refs = (ULONG)InterlockedDecrement(&m->refs);
    if (!refs) {
        if (m->orig) m->orig->lpVtbl->Release(m->orig);
        /* Keep the singleton dummy alive so AddItem state survives re-QI. */
        if (m != g_dummy_langbar)
            HeapFree(GetProcessHeap(), 0, m);
    }
    return refs;
}

static HRESULT WINAPI langbaritemmgr_EnumItems(ITfLangBarItemMgr *iface, IEnumTfLangBarItems **ppEnum)
{
    MsctfLangBarItemMgrWrap *m = langbaritemmgr_from_iface(iface);
    HRESULT hr;
    MsctfLangBarItemEnum *e;

    if (!ppEnum) return E_POINTER;
    *ppEnum = 0;
    if (!m->orig) {
        e = langbar_make_item_enum(m);
        if (!e) return E_OUTOFMEMORY;
        *ppEnum = &e->iface;
        log_ptr_hr("MsctfShim: LangBarItemMgr EnumItems dummy", iface, S_OK);
        return S_OK;
    }
    hr = m->orig->lpVtbl->EnumItems(m->orig, ppEnum);
    log_ptr_hr("MsctfShim: LangBarItemMgr EnumItems", iface, hr);
    return hr;
}

static HRESULT WINAPI langbaritemmgr_GetItem(ITfLangBarItemMgr *iface, REFGUID rguid, ITfLangBarItem **ppItem)
{
    MsctfLangBarItemMgrWrap *m = langbaritemmgr_from_iface(iface);
    HRESULT hr;
    ITfLangBarItem *found;

    if (rguid) {
        char buf[192];
        wsprintfA(buf, "MsctfShim: LangBarItemMgr GetItem guid=%s\r\n", guid_string(rguid));
        log_line(buf);
    }
    if (!ppItem) return E_POINTER;
    *ppItem = 0;
    if (!m->orig) {
        found = langbar_find_item_by_guid(m, rguid);
        if (!found) {
            log_ptr_hr("MsctfShim: LangBarItemMgr GetItem dummy missing", iface, S_FALSE);
            return S_FALSE;
        }
        found->lpVtbl->AddRef(found);
        *ppItem = found;
        log_ptr_hr("MsctfShim: LangBarItemMgr GetItem dummy", iface, S_OK);
        return S_OK;
    }
    hr = m->orig->lpVtbl->GetItem(m->orig, rguid, ppItem);
    log_ptr_hr("MsctfShim: LangBarItemMgr GetItem", iface, hr);
    return hr;
}

static HRESULT WINAPI langbaritemmgr_AddItem(ITfLangBarItemMgr *iface, ITfLangBarItem *punk)
{
    MsctfLangBarItemMgrWrap *m = langbaritemmgr_from_iface(iface);
    HRESULT hr;
    HRESULT status_hr;
    TF_LANGBARITEMINFO info;
    DWORD status = 0;
    char buf[256];
    ULONG i;

    if (!m->orig) {
        if (!punk) return E_INVALIDARG;
        if (m->item_count >= MSCTF_LANG_BAR_ITEM_MAX) return E_OUTOFMEMORY;
        ZeroMemory(&info, sizeof(info));
        hr = punk->lpVtbl->GetInfo(punk, &info);
        if (hr != S_OK) {
            log_ptr2_hr("MsctfShim: LangBarItemMgr AddItem dummy GetInfo", iface, punk, hr);
            return hr;
        }
        status_hr = punk->lpVtbl->GetStatus(punk, &status);
        wsprintfA(buf, "MsctfShim: LangBarItemMgr AddItem dummy item guid=%s status_hr=0x%08lX status=0x%08lX\r\n",
                  guid_string(&info.guidItem), (unsigned long)status_hr, (unsigned long)status);
        log_line(buf);
        for (i = 0; i < m->item_count; i++) {
            if (IsEqualGUID(&m->items[i].guid, &info.guidItem)) {
                log_ptr2_hr("MsctfShim: LangBarItemMgr AddItem dummy duplicate", iface, punk, S_OK);
                return S_OK;
            }
        }
        punk->lpVtbl->AddRef(punk);
        m->items[m->item_count].item = punk;
        m->items[m->item_count].guid = info.guidItem;
        m->item_count++;
        langbar_notify_sinks(m, 1);
        log_ptr2_hr("MsctfShim: LangBarItemMgr AddItem dummy", iface, punk, S_OK);
        return S_OK;
    }
    hr = m->orig->lpVtbl->AddItem(m->orig, punk);
    log_ptr2_hr("MsctfShim: LangBarItemMgr AddItem", iface, punk, hr);
    return hr;
}

static HRESULT WINAPI langbaritemmgr_RemoveItem(ITfLangBarItemMgr *iface, ITfLangBarItem *punk)
{
    MsctfLangBarItemMgrWrap *m = langbaritemmgr_from_iface(iface);
    HRESULT hr;
    int index;
    TF_LANGBARITEMINFO info;
    DWORD status = 0;
    HRESULT info_hr = E_FAIL;
    HRESULT status_hr = E_FAIL;

    if (!m->orig) {
        ZeroMemory(&info, sizeof(info));
        index = langbar_find_item_index_by_ptr(m, punk);
        if (index < 0 && punk && punk->lpVtbl && punk->lpVtbl->GetInfo) {
            hr = punk->lpVtbl->GetInfo(punk, &info);
            info_hr = hr;
            if (hr == S_OK)
                index = langbar_find_item_index_by_guid(m, &info.guidItem);
        }
        if (index < 0) return E_INVALIDARG;
        if (punk && punk->lpVtbl && punk->lpVtbl->GetInfo && info_hr != S_OK)
            info_hr = punk->lpVtbl->GetInfo(punk, &info);
        if (punk && punk->lpVtbl && punk->lpVtbl->GetStatus)
            status_hr = punk->lpVtbl->GetStatus(punk, &status);
        {
            char buf[224];
            wsprintfA(buf, "MsctfShim: LangBarItemMgr RemoveItem dummy item guid=%s info_hr=0x%08lX status_hr=0x%08lX status=0x%08lX index=%d\r\n",
                      info_hr == S_OK ? guid_string(&info.guidItem) : "(unknown)",
                      (unsigned long)info_hr, (unsigned long)status_hr,
                      (unsigned long)status, index);
            log_line(buf);
        }
        langbar_remove_item_at(m, (ULONG)index);
        langbar_notify_sinks(m, 1);
        log_ptr2_hr("MsctfShim: LangBarItemMgr RemoveItem dummy", iface, punk, S_OK);
        return S_OK;
    }
    hr = m->orig->lpVtbl->RemoveItem(m->orig, punk);
    log_ptr2_hr("MsctfShim: LangBarItemMgr RemoveItem", iface, punk, hr);
    return hr;
}

static HRESULT WINAPI langbaritemmgr_AdviseItemSink(ITfLangBarItemMgr *iface, ITfLangBarItemSink *punk, DWORD *pdwCookie, REFGUID rguidItem)
{
    MsctfLangBarItemMgrWrap *m = langbaritemmgr_from_iface(iface);
    HRESULT hr;
    LangBarSinkEntry *entry;

    if (rguidItem) {
        char buf[192];
        wsprintfA(buf, "MsctfShim: LangBarItemMgr AdviseItemSink guid=%s\r\n", guid_string(rguidItem));
        log_line(buf);
    }
    if (!m->orig) {
        if (!punk || !pdwCookie || !rguidItem) return E_INVALIDARG;
        if (m->sink_count >= MSCTF_LANG_BAR_SINK_MAX) return E_OUTOFMEMORY;
        if (!m->next_sink_cookie) m->next_sink_cookie = 1;
        entry = &m->sinks[m->sink_count++];
        punk->lpVtbl->AddRef(punk);
        entry->sink = punk;
        entry->guid = *rguidItem;
        entry->cookie = m->next_sink_cookie++;
        *pdwCookie = entry->cookie;
        log_ptr_hr("MsctfShim: LangBarItemMgr AdviseItemSink dummy", iface, S_OK);
        return S_OK;
    }
    hr = m->orig->lpVtbl->AdviseItemSink(m->orig, punk, pdwCookie, rguidItem);
    log_ptr_hr("MsctfShim: LangBarItemMgr AdviseItemSink", iface, hr);
    return hr;
}

static HRESULT WINAPI langbaritemmgr_UnadviseItemSink(ITfLangBarItemMgr *iface, DWORD dwCookie)
{
    MsctfLangBarItemMgrWrap *m = langbaritemmgr_from_iface(iface);
    HRESULT hr;
    ULONG i;

    if (!m->orig) {
        for (i = 0; i < m->sink_count; i++) {
            if (m->sinks[i].cookie == dwCookie) {
                if (m->sinks[i].sink)
                    m->sinks[i].sink->lpVtbl->Release(m->sinks[i].sink);
                for (; i + 1 < m->sink_count; i++)
                    m->sinks[i] = m->sinks[i + 1];
                m->sink_count--;
                log_ptr_hr("MsctfShim: LangBarItemMgr UnadviseItemSink dummy", iface, S_OK);
                return S_OK;
            }
        }
        log_ptr_hr("MsctfShim: LangBarItemMgr UnadviseItemSink dummy missing", iface, E_INVALIDARG);
        return E_INVALIDARG;
    }
    hr = m->orig->lpVtbl->UnadviseItemSink(m->orig, dwCookie);
    log_ptr_hr("MsctfShim: LangBarItemMgr UnadviseItemSink", iface, hr);
    return hr;
}

static HRESULT WINAPI langbaritemmgr_GetItemFloatingRect(ITfLangBarItemMgr *iface, DWORD dwThreadId, REFGUID rguid, RECT *prc)
{
    MsctfLangBarItemMgrWrap *m = langbaritemmgr_from_iface(iface);
    HRESULT hr;
    if (!m->orig) {
        if (prc) SetRectEmpty(prc);
        log_ptr_hr("MsctfShim: LangBarItemMgr GetItemFloatingRect dummy", iface, S_OK);
        return S_OK;
    }
    hr = m->orig->lpVtbl->GetItemFloatingRect(m->orig, dwThreadId, rguid, prc);
    log_ptr_hr("MsctfShim: LangBarItemMgr GetItemFloatingRect", iface, hr);
    return hr;
}

static HRESULT WINAPI langbaritemmgr_GetItemsStatus(ITfLangBarItemMgr *iface, ULONG ulCount, const GUID *prgguid, DWORD *pdwStatus)
{
    MsctfLangBarItemMgrWrap *m = langbaritemmgr_from_iface(iface);
    HRESULT hr;
    ULONG i;
    ITfLangBarItem *item;

    if (!m->orig) {
        if (!pdwStatus) return E_POINTER;
        if (ulCount && !prgguid) return E_POINTER;
        for (i = 0; i < ulCount; i++)
            pdwStatus[i] = 0;
        if (!ulCount) {
            log_ptr_hr("MsctfShim: LangBarItemMgr GetItemsStatus dummy empty", iface, S_OK);
            return S_OK;
        }
        if (ulCount == 1 && prgguid) {
            item = langbar_find_item_by_guid(m, prgguid);
            if (item && item->lpVtbl->GetStatus)
                item->lpVtbl->GetStatus(item, pdwStatus);
        } else {
            for (i = 0; i < ulCount; i++) {
                item = langbar_find_item_by_guid(m, &prgguid[i]);
                if (item && item->lpVtbl->GetStatus)
                    item->lpVtbl->GetStatus(item, &pdwStatus[i]);
                else
                    pdwStatus[i] = 0;
            }
        }
        log_ptr_hr("MsctfShim: LangBarItemMgr GetItemsStatus dummy", iface, S_OK);
        return S_OK;
    }
    hr = m->orig->lpVtbl->GetItemsStatus(m->orig, ulCount, prgguid, pdwStatus);
    log_ptr_hr("MsctfShim: LangBarItemMgr GetItemsStatus", iface, hr);
    return hr;
}

static HRESULT WINAPI langbaritemmgr_GetItemNum(ITfLangBarItemMgr *iface, ULONG *pulCount)
{
    MsctfLangBarItemMgrWrap *m = langbaritemmgr_from_iface(iface);
    HRESULT hr;
    if (!m->orig) {
        if (pulCount) *pulCount = m->item_count;
        log_ptr_hr("MsctfShim: LangBarItemMgr GetItemNum dummy", iface, S_OK);
        return S_OK;
    }
    hr = m->orig->lpVtbl->GetItemNum(m->orig, pulCount);
    log_ptr_hr("MsctfShim: LangBarItemMgr GetItemNum", iface, hr);
    return hr;
}

static HRESULT WINAPI langbaritemmgr_GetItems(ITfLangBarItemMgr *iface, ULONG ulCount, ITfLangBarItem **ppItem, TF_LANGBARITEMINFO *pInfo, DWORD *pdwStatus, ULONG *pcFetched)
{
    MsctfLangBarItemMgrWrap *m = langbaritemmgr_from_iface(iface);
    HRESULT hr;
    ULONG i, fetched = 0;

    if (!m->orig) {
        if (pcFetched) *pcFetched = 0;
        if (!ulCount) {
            log_ptr_hr("MsctfShim: LangBarItemMgr GetItems dummy empty", iface, S_OK);
            return S_OK;
        }
        if (!ppItem) return E_POINTER;
        for (i = 0; i < ulCount; i++) {
            ppItem[i] = 0;
            if (pdwStatus) pdwStatus[i] = 0;
            if (pInfo) ZeroMemory(&pInfo[i], sizeof(pInfo[i]));
        }
        for (i = 0; i < ulCount && i < m->item_count; i++) {
            ITfLangBarItem *item = m->items[i].item;
            item->lpVtbl->AddRef(item);
            ppItem[i] = item;
            if (pInfo)
                item->lpVtbl->GetInfo(item, &pInfo[i]);
            if (pdwStatus)
                item->lpVtbl->GetStatus(item, &pdwStatus[i]);
            fetched++;
        }
        if (pcFetched) *pcFetched = fetched;
        log_ptr_hr("MsctfShim: LangBarItemMgr GetItems dummy", iface, fetched ? S_OK : S_FALSE);
        return fetched ? S_OK : S_FALSE;
    }
    hr = m->orig->lpVtbl->GetItems(m->orig, ulCount, ppItem, pInfo, pdwStatus, pcFetched);
    log_ptr_hr("MsctfShim: LangBarItemMgr GetItems", iface, hr);
    return hr;
}

static HRESULT WINAPI langbaritemmgr_AdviseItemsSink(ITfLangBarItemMgr *iface, ULONG ulCount, ITfLangBarItemSink **ppunk, const GUID *pguidItem, DWORD *pdwCookie)
{
    MsctfLangBarItemMgrWrap *m = langbaritemmgr_from_iface(iface);
    HRESULT hr;
    ULONG i;

    if (!m->orig) {
        if (!ppunk || !pguidItem || !pdwCookie || !ulCount) return E_INVALIDARG;
        for (i = 0; i < ulCount; i++) {
            hr = langbaritemmgr_AdviseItemSink(iface, ppunk[i], &pdwCookie[i], &pguidItem[i]);
            if (hr != S_OK) return hr;
        }
        log_ptr_hr("MsctfShim: LangBarItemMgr AdviseItemsSink dummy", iface, S_OK);
        return S_OK;
    }
    hr = m->orig->lpVtbl->AdviseItemsSink(m->orig, ulCount, ppunk, pguidItem, pdwCookie);
    log_ptr_hr("MsctfShim: LangBarItemMgr AdviseItemsSink", iface, hr);
    return hr;
}

static HRESULT WINAPI langbaritemmgr_UnadviseItemsSink(ITfLangBarItemMgr *iface, ULONG ulCount, DWORD *pdwCookie)
{
    MsctfLangBarItemMgrWrap *m = langbaritemmgr_from_iface(iface);
    HRESULT hr;
    ULONG i;

    if (!m->orig) {
        if (!pdwCookie || !ulCount) return E_INVALIDARG;
        for (i = 0; i < ulCount; i++) {
            hr = langbaritemmgr_UnadviseItemSink(iface, pdwCookie[i]);
            if (hr != S_OK) return hr;
        }
        log_ptr_hr("MsctfShim: LangBarItemMgr UnadviseItemsSink dummy", iface, S_OK);
        return S_OK;
    }
    hr = m->orig->lpVtbl->UnadviseItemsSink(m->orig, ulCount, pdwCookie);
    log_ptr_hr("MsctfShim: LangBarItemMgr UnadviseItemsSink", iface, hr);
    return hr;
}

static const ITfLangBarItemMgrVtbl g_langbaritemmgr_vtbl = {
    langbaritemmgr_QI, langbaritemmgr_AddRef, langbaritemmgr_Release,
    langbaritemmgr_EnumItems, langbaritemmgr_GetItem, langbaritemmgr_AddItem, langbaritemmgr_RemoveItem,
    langbaritemmgr_AdviseItemSink, langbaritemmgr_UnadviseItemSink, langbaritemmgr_GetItemFloatingRect,
    langbaritemmgr_GetItemsStatus, langbaritemmgr_GetItemNum, langbaritemmgr_GetItems,
    langbaritemmgr_AdviseItemsSink, langbaritemmgr_UnadviseItemsSink
};

static MsctfLangBarItemMgrWrap *make_langbaritemmgr(ITfLangBarItemMgr *orig, void *owner)
{
    MsctfLangBarItemMgrWrap *m = (MsctfLangBarItemMgrWrap *)HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, sizeof(*m));
    if (!m) return 0;
    m->iface.lpVtbl = &g_langbaritemmgr_vtbl;
    m->refs = 1;
    m->orig = orig;
    m->owner = owner;
    m->next_sink_cookie = 1;
    if (m->orig) m->orig->lpVtbl->AddRef(m->orig);
    return m;
}

static HRESULT wrap_langbaritemmgr_result(ITfLangBarItemMgr *orig, void *owner, void **ppv, const char *label)
{
    MsctfLangBarItemMgrWrap *wrap;
    if (!ppv) return E_POINTER;
    if (!orig && pTF_CreateLangBarItemMgr) {
        HRESULT hr = pTF_CreateLangBarItemMgr(&orig);
        log_ptr_hr("MsctfShim: TF_CreateLangBarItemMgr fallback", orig, hr);
    }
    /* Dummy (no real langbar mgr from Wine): hand out a single shared instance
     * so item add/remove/count stays consistent across ATOK's re-acquisitions. */
    if (!orig) {
        if (!g_dummy_langbar) {
            g_dummy_langbar = make_langbaritemmgr(0, owner);
            if (!g_dummy_langbar) return E_OUTOFMEMORY;
        }
        g_dummy_langbar->iface.lpVtbl->AddRef(&g_dummy_langbar->iface);
        *ppv = &g_dummy_langbar->iface;
        log_ptr_hr(label, *ppv, S_OK);
        return S_OK;
    }
    wrap = make_langbaritemmgr(orig, owner);
    if (!wrap) {
        if (orig) orig->lpVtbl->Release(orig);
        return E_OUTOFMEMORY;
    }
    *ppv = &wrap->iface;
    if (orig) {
        log_ptr2_hr(label, orig, *ppv, S_OK);
        orig->lpVtbl->Release(orig);
    } else {
        log_ptr_hr(label, *ppv, S_OK);
    }
    return S_OK;
}

/* ITfCategoryMgr */
static inline MsctfCategoryMgrWrap *categorymgr_from_iface(ITfCategoryMgr *iface)
{
    return (MsctfCategoryMgrWrap *)iface;
}

static HRESULT WINAPI categorymgr_QI(ITfCategoryMgr *iface, REFIID riid, void **ppv)
{
    if (IsEqualIID(riid, &IID_IUnknown) || IsEqualIID(riid, &IID_LOCAL_ITfCategoryMgr)) {
        *ppv = iface;
        iface->lpVtbl->AddRef(iface);
        return S_OK;
    }
    *ppv = 0;
    return E_NOINTERFACE;
}

static ULONG WINAPI categorymgr_AddRef(ITfCategoryMgr *iface)
{
    return (ULONG)InterlockedIncrement(&categorymgr_from_iface(iface)->refs);
}

static ULONG WINAPI categorymgr_Release(ITfCategoryMgr *iface)
{
    MsctfCategoryMgrWrap *c = categorymgr_from_iface(iface);
    ULONG refs = (ULONG)InterlockedDecrement(&c->refs);
    if (!refs) HeapFree(GetProcessHeap(), 0, c);
    return refs;
}

static HRESULT WINAPI categorymgr_RegisterCategory(ITfCategoryMgr *iface, REFCLSID rclsid, REFGUID rcatid, REFGUID rguid)
{
    char buf[256];
    (void)iface;
    wsprintfA(buf, "MsctfShim: CategoryMgr RegisterCategory clsid=%s catid=%s guid=%s\r\n",
              guid_string(rclsid), guid_string(rcatid), guid_string(rguid));
    log_line(buf);
    return S_OK;
}

static HRESULT WINAPI categorymgr_UnregisterCategory(ITfCategoryMgr *iface, REFCLSID rclsid, REFGUID rcatid, REFGUID rguid)
{
    char buf[256];
    (void)iface;
    wsprintfA(buf, "MsctfShim: CategoryMgr UnregisterCategory clsid=%s catid=%s guid=%s\r\n",
              guid_string(rclsid), guid_string(rcatid), guid_string(rguid));
    log_line(buf);
    return S_OK;
}

static HRESULT WINAPI categorymgr_EnumCategoriesInItem(ITfCategoryMgr *iface, REFGUID rguid, IEnumGUID **ppEnum)
{
    char buf[160];
    (void)iface;
    wsprintfA(buf, "MsctfShim: CategoryMgr EnumCategoriesInItem guid=%s\r\n", guid_string(rguid));
    log_line(buf);
    if (ppEnum) *ppEnum = 0;
    return S_FALSE;
}

static HRESULT WINAPI categorymgr_EnumItemsInCategory(ITfCategoryMgr *iface, REFGUID rcatid, IEnumGUID **ppEnum)
{
    char buf[160];
    (void)iface;
    wsprintfA(buf, "MsctfShim: CategoryMgr EnumItemsInCategory catid=%s\r\n", guid_string(rcatid));
    log_line(buf);
    if (ppEnum) *ppEnum = 0;
    return S_FALSE;
}

static HRESULT WINAPI categorymgr_FindClosestCategory(ITfCategoryMgr *iface, REFGUID rguid, GUID *pcatid,
                                                       const GUID **ppcatidList, ULONG ulCount)
{
    char buf[192];
    (void)iface; (void)ppcatidList; (void)ulCount;
    wsprintfA(buf, "MsctfShim: CategoryMgr FindClosestCategory guid=%s\r\n", guid_string(rguid));
    log_line(buf);
    if (pcatid) ZeroMemory(pcatid, sizeof(*pcatid));
    return S_FALSE;
}

static HRESULT WINAPI categorymgr_RegisterGUIDDescription(ITfCategoryMgr *iface, REFCLSID rclsid, REFGUID rguid,
                                                          const WCHAR *pchDesc, ULONG cch)
{
    char buf[256];
    (void)iface;
    wsprintfA(buf, "MsctfShim: CategoryMgr RegisterGUIDDescription clsid=%s guid=%s cch=%lu\r\n",
              guid_string(rclsid), guid_string(rguid), (unsigned long)cch);
    log_line(buf);
    (void)pchDesc;
    return S_OK;
}

static HRESULT WINAPI categorymgr_UnregisterGUIDDescription(ITfCategoryMgr *iface, REFCLSID rclsid, REFGUID rguid)
{
    char buf[192];
    (void)iface;
    wsprintfA(buf, "MsctfShim: CategoryMgr UnregisterGUIDDescription clsid=%s guid=%s\r\n",
              guid_string(rclsid), guid_string(rguid));
    log_line(buf);
    return S_OK;
}

static HRESULT WINAPI categorymgr_GetGUIDDescription(ITfCategoryMgr *iface, REFGUID rguid, BSTR *pbstrDesc)
{
    static const WCHAR empty[] = L"";
    char buf[160];
    (void)iface;
    wsprintfA(buf, "MsctfShim: CategoryMgr GetGUIDDescription guid=%s\r\n", guid_string(rguid));
    log_line(buf);
    if (!pbstrDesc) return E_POINTER;
    ensure_oleaut32();
    if (!pSysAllocString) return E_FAIL;
    *pbstrDesc = pSysAllocString(empty);
    return *pbstrDesc ? S_OK : E_OUTOFMEMORY;
}

static HRESULT WINAPI categorymgr_RegisterGUIDDWORD(ITfCategoryMgr *iface, REFCLSID rclsid, REFGUID rguid, DWORD dw)
{
    char buf[192];
    (void)iface;
    wsprintfA(buf, "MsctfShim: CategoryMgr RegisterGUIDDWORD clsid=%s guid=%s dw=%lu\r\n",
              guid_string(rclsid), guid_string(rguid), (unsigned long)dw);
    log_line(buf);
    return S_OK;
}

static HRESULT WINAPI categorymgr_UnregisterGUIDDWORD(ITfCategoryMgr *iface, REFCLSID rclsid, REFGUID rguid)
{
    char buf[160];
    (void)iface;
    wsprintfA(buf, "MsctfShim: CategoryMgr UnregisterGUIDDWORD clsid=%s guid=%s\r\n",
              guid_string(rclsid), guid_string(rguid));
    log_line(buf);
    return S_OK;
}

static HRESULT WINAPI categorymgr_GetGUIDDWORD(ITfCategoryMgr *iface, REFGUID rguid, DWORD *pdw)
{
    char buf[160];
    (void)iface;
    wsprintfA(buf, "MsctfShim: CategoryMgr GetGUIDDWORD guid=%s\r\n", guid_string(rguid));
    log_line(buf);
    if (pdw) *pdw = 0;
    return S_OK;
}

static HRESULT WINAPI categorymgr_RegisterGUID(ITfCategoryMgr *iface, REFGUID rguid, TfGuidAtom *pguidatom)
{
    char buf[160];
    ULONG i;
    (void)iface;
    wsprintfA(buf, "MsctfShim: CategoryMgr RegisterGUID guid=%s\r\n", guid_string(rguid));
    log_line(buf);
    if (!rguid || !pguidatom) return E_POINTER;
    for (i = 0; i < g_guid_atom_count; i++) {
        if (IsEqualGUID(&g_guid_atoms[i].guid, rguid)) {
            *pguidatom = g_guid_atoms[i].atom;
            wsprintfA(buf, "MsctfShim: CategoryMgr RegisterGUID existing atom=%lu\r\n",
                      (unsigned long)*pguidatom);
            log_line(buf);
            return S_OK;
        }
    }
    if (g_guid_atom_count >= MSCTF_GUID_ATOM_MAX) return E_OUTOFMEMORY;
    g_guid_atoms[g_guid_atom_count].guid = *rguid;
    g_guid_atoms[g_guid_atom_count].atom = g_next_guid_atom++;
    *pguidatom = g_guid_atoms[g_guid_atom_count].atom;
    g_guid_atom_count++;
    wsprintfA(buf, "MsctfShim: CategoryMgr RegisterGUID atom=%lu\r\n", (unsigned long)*pguidatom);
    log_line(buf);
    return S_OK;
}

static HRESULT WINAPI categorymgr_GetGUID(ITfCategoryMgr *iface, TfGuidAtom guidatom, GUID *pguid)
{
    char buf[160];
    ULONG i;
    (void)iface;
    wsprintfA(buf, "MsctfShim: CategoryMgr GetGUID atom=%lu\r\n", (unsigned long)guidatom);
    log_line(buf);
    if (!pguid) return E_POINTER;
    for (i = 0; i < g_guid_atom_count; i++) {
        if (g_guid_atoms[i].atom == guidatom) {
            *pguid = g_guid_atoms[i].guid;
            return S_OK;
        }
    }
    ZeroMemory(pguid, sizeof(*pguid));
    return E_INVALIDARG;
}

static HRESULT WINAPI categorymgr_IsEqualTfGuidAtom(ITfCategoryMgr *iface, TfGuidAtom guidatom, REFGUID rguid,
                                                    BOOL *pfEqual)
{
    char buf[192];
    ULONG i;
    (void)iface;
    wsprintfA(buf, "MsctfShim: CategoryMgr IsEqualTfGuidAtom atom=%lu guid=%s\r\n",
              (unsigned long)guidatom, guid_string(rguid));
    log_line(buf);
    if (!pfEqual) return E_POINTER;
    *pfEqual = FALSE;
    if (!rguid) return E_POINTER;
    for (i = 0; i < g_guid_atom_count; i++) {
        if (g_guid_atoms[i].atom == guidatom) {
            *pfEqual = IsEqualGUID(&g_guid_atoms[i].guid, rguid);
            return S_OK;
        }
    }
    return S_OK;
}

static const ITfCategoryMgrVtbl g_categorymgr_vtbl = {
    categorymgr_QI, categorymgr_AddRef, categorymgr_Release,
    categorymgr_RegisterCategory, categorymgr_UnregisterCategory, categorymgr_EnumCategoriesInItem,
    categorymgr_EnumItemsInCategory, categorymgr_FindClosestCategory, categorymgr_RegisterGUIDDescription,
    categorymgr_UnregisterGUIDDescription, categorymgr_GetGUIDDescription, categorymgr_RegisterGUIDDWORD,
    categorymgr_UnregisterGUIDDWORD, categorymgr_GetGUIDDWORD, categorymgr_RegisterGUID, categorymgr_GetGUID,
    categorymgr_IsEqualTfGuidAtom
};

static ITfCategoryMgr *make_categorymgr(void)
{
    MsctfCategoryMgrWrap *c = (MsctfCategoryMgrWrap *)HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, sizeof(*c));
    if (!c) return 0;
    c->iface.lpVtbl = &g_categorymgr_vtbl;
    c->refs = 1;
    return &c->iface;
}

static const IEnumTfFunctionProvidersVtbl g_provider_enum_vtbl;

static inline MsctfFunctionProviderWrap *provider_from_iface(ITfFunctionProvider *iface)
{
    return (MsctfFunctionProviderWrap *)iface;
}

static inline MsctfFunctionProviderEnumWrap *provider_enum_from_iface(IEnumTfFunctionProviders *iface)
{
    return (MsctfFunctionProviderEnumWrap *)iface;
}

static HRESULT WINAPI provider_QI(ITfFunctionProvider *iface, REFIID riid, void **ppv)
{
    if (IsEqualIID(riid, &IID_IUnknown) || IsEqualIID(riid, &IID_LOCAL_ITfFunctionProvider)) {
        *ppv = iface;
        iface->lpVtbl->AddRef(iface);
        return S_OK;
    }
    *ppv = 0;
    return E_NOINTERFACE;
}

static ULONG WINAPI provider_AddRef(ITfFunctionProvider *iface)
{
    return (ULONG)InterlockedIncrement(&provider_from_iface(iface)->refs);
}

static ULONG WINAPI provider_Release(ITfFunctionProvider *iface)
{
    MsctfFunctionProviderWrap *p = provider_from_iface(iface);
    ULONG r = (ULONG)InterlockedDecrement(&p->refs);
    if (!r) {
        HeapFree(GetProcessHeap(), 0, p);
    }
    return r;
}

static HRESULT WINAPI provider_GetType(ITfFunctionProvider *iface, GUID *guid)
{
    MsctfFunctionProviderWrap *p = provider_from_iface(iface);
    char buf[128];
    if (!guid) return E_POINTER;
    *guid = p->type_guid;
    wsprintfA(buf, "MsctfShim: FunctionProvider GetType type=%s\r\n", guid_string(&p->type_guid));
    log_line(buf);
    return S_OK;
}

static HRESULT WINAPI provider_GetDescription(ITfFunctionProvider *iface, BSTR *desc)
{
    static const WCHAR desc_text[] = L"ATOK TIP Function Provider";
    (void)iface;
    if (!desc) return E_POINTER;
    ensure_oleaut32();
    if (!pSysAllocString) return E_FAIL;
    *desc = pSysAllocString(desc_text);
    if (!*desc) return E_OUTOFMEMORY;
    log_line("MsctfShim: FunctionProvider GetDescription\r\n");
    return S_OK;
}

static HRESULT WINAPI provider_GetFunction(ITfFunctionProvider *iface, REFGUID guid, REFIID riid, IUnknown **func)
{
    MsctfFunctionProviderWrap *p = provider_from_iface(iface);
    char buf[224];
    if (!func) return E_POINTER;
    *func = 0;
    wsprintfA(buf, "MsctfShim: FunctionProvider GetFunction type=%s query=%s want=%s\r\n",
              guid_string(&p->type_guid),
              guid ? guid_string(guid) : "null",
              riid ? guid_string(riid) : "null");
    log_line(buf);
    if (IsEqualIID(riid, &IID_IUnknown) || IsEqualIID(riid, &IID_LOCAL_ITfFunctionProvider)) {
        *func = (IUnknown *)iface;
        iface->lpVtbl->AddRef(iface);
        return S_OK;
    }
    return E_NOINTERFACE;
}

static const ITfFunctionProviderVtbl g_provider_vtbl = {
    provider_QI, provider_AddRef, provider_Release, provider_GetType, provider_GetDescription, provider_GetFunction
};

static HRESULT WINAPI provider_enum_QI(IEnumTfFunctionProviders *iface, REFIID riid, void **ppv)
{
    if (IsEqualIID(riid, &IID_IUnknown) || IsEqualIID(riid, &IID_LOCAL_IEnumTfFunctionProviders)) {
        *ppv = iface;
        iface->lpVtbl->AddRef(iface);
        return S_OK;
    }
    *ppv = 0;
    return E_NOINTERFACE;
}

static ULONG WINAPI provider_enum_AddRef(IEnumTfFunctionProviders *iface)
{
    return (ULONG)InterlockedIncrement(&provider_enum_from_iface(iface)->refs);
}

static ULONG WINAPI provider_enum_Release(IEnumTfFunctionProviders *iface)
{
    MsctfFunctionProviderEnumWrap *e = provider_enum_from_iface(iface);
    ULONG r = (ULONG)InterlockedDecrement(&e->refs);
    if (!r) {
        if (e->provider) e->provider->iface.lpVtbl->Release(&e->provider->iface);
        HeapFree(GetProcessHeap(), 0, e);
    }
    return r;
}

static HRESULT WINAPI provider_enum_Clone(IEnumTfFunctionProviders *iface, IEnumTfFunctionProviders **ret)
{
    MsctfFunctionProviderEnumWrap *src = provider_enum_from_iface(iface);
    MsctfFunctionProviderEnumWrap *dst = (MsctfFunctionProviderEnumWrap *)HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, sizeof(*dst));
    if (!dst) return E_OUTOFMEMORY;
    dst->iface.lpVtbl = &g_provider_enum_vtbl;
    dst->refs = 1;
    dst->tm = src->tm;
    dst->returned = src->returned;
    dst->provider = src->provider;
    if (dst->provider) dst->provider->iface.lpVtbl->AddRef(&dst->provider->iface);
    *ret = &dst->iface;
    return S_OK;
}

static HRESULT WINAPI provider_enum_Next(IEnumTfFunctionProviders *iface, ULONG count, ITfFunctionProvider **prov, ULONG *fetched)
{
    MsctfFunctionProviderEnumWrap *e = provider_enum_from_iface(iface);
    if (fetched) *fetched = 0;
    if (!prov || !count) return E_INVALIDARG;
    if (e->returned || !e->provider) return S_FALSE;
    prov[0] = &e->provider->iface;
    prov[0]->lpVtbl->AddRef(prov[0]);
    e->returned = TRUE;
    if (fetched) *fetched = 1;
    return S_OK;
}

static HRESULT WINAPI provider_enum_Reset(IEnumTfFunctionProviders *iface)
{
    provider_enum_from_iface(iface)->returned = FALSE;
    return S_OK;
}

static HRESULT WINAPI provider_enum_Skip(IEnumTfFunctionProviders *iface, ULONG count)
{
    MsctfFunctionProviderEnumWrap *e = provider_enum_from_iface(iface);
    if (count && !e->returned) {
        e->returned = TRUE;
        return S_OK;
    }
    return S_FALSE;
}

static const IEnumTfFunctionProvidersVtbl g_provider_enum_vtbl = {
    provider_enum_QI, provider_enum_AddRef, provider_enum_Release,
    provider_enum_Clone, provider_enum_Next, provider_enum_Reset, provider_enum_Skip
};

static MsctfFunctionProviderWrap *make_function_provider(MsctfThreadMgrWrap *tm, REFCLSID clsid)
{
    MsctfFunctionProviderWrap *p = (MsctfFunctionProviderWrap *)HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, sizeof(*p));
    if (!p) return 0;
    p->iface.lpVtbl = &g_provider_vtbl;
    p->refs = 1;
    p->tm = tm;
    if (clsid) p->clsid = *clsid;
    if (clsid) p->type_guid = *clsid;
    else p->type_guid = GUID_ATOK_PROFILE;
    return p;
}

static IEnumTfFunctionProviders *make_function_provider_enum(MsctfThreadMgrWrap *tm)
{
    MsctfFunctionProviderEnumWrap *e = (MsctfFunctionProviderEnumWrap *)HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, sizeof(*e));
    if (!e) return 0;
    if (!tm->provider) {
        tm->provider = make_function_provider(tm, 0);
        if (!tm->provider) {
            HeapFree(GetProcessHeap(), 0, e);
            return 0;
        }
    }
    e->iface.lpVtbl = &g_provider_enum_vtbl;
    e->refs = 1;
    e->tm = tm;
    e->returned = FALSE;
    e->provider = tm->provider;
    e->provider->iface.lpVtbl->AddRef(&e->provider->iface);
    return &e->iface;
}

/* ITfKeystrokeMgr proxy */
static HRESULT WINAPI km_QI(ITfKeystrokeMgr *iface, REFIID riid, void **ppv)
{
    if (IsEqualIID(riid, &IID_IUnknown) || IsEqualIID(riid, &IID_ITfKeystrokeMgr)) {
        *ppv = iface;
        iface->lpVtbl->AddRef(iface);
        return S_OK;
    }
    *ppv = 0;
    return E_NOINTERFACE;
}

static ULONG WINAPI km_AddRef(ITfKeystrokeMgr *iface)
{
    return (ULONG)InterlockedIncrement(&km_from_iface(iface)->refs);
}

static ULONG WINAPI km_Release(ITfKeystrokeMgr *iface)
{
    MsctfKeystrokeWrap *k = km_from_iface(iface);
    ULONG r = (ULONG)InterlockedDecrement(&k->refs);
    if (!r) {
        if (k->sink) k->sink->lpVtbl->Release(k->sink);
        HeapFree(GetProcessHeap(), 0, k);
    }
    return r;
}

static HRESULT WINAPI km_AdviseKeyEventSink(ITfKeystrokeMgr *iface, TfClientId tid, ITfKeyEventSink *pSink, BOOL fForeground)
{
    MsctfKeystrokeWrap *k = km_from_iface(iface);
    char buf[192];
    if (k->sink) k->sink->lpVtbl->Release(k->sink);
    k->sink = pSink;
    k->tid = tid;
    k->foreground = fForeground;
    k->clsid = CLSID_ATOK_TIP;
    if (k->sink) k->sink->lpVtbl->AddRef(k->sink);
    /* Remember globally so any km wrapper instance can route keys to it. */
    if (g_key_sink) g_key_sink->lpVtbl->Release(g_key_sink);
    g_key_sink = pSink;
    g_key_tid = tid;
    if (g_key_sink) g_key_sink->lpVtbl->AddRef(g_key_sink);
    if (k->sink && k->foreground && k->sink->lpVtbl->OnSetFocus) {
        k->sink->lpVtbl->OnSetFocus(k->sink, TRUE);
    }
    wsprintfA(buf, "MsctfShim: AdviseKeyEventSink iface=%p sink=%p foreground=%d g_key_sink=%p\r\n", iface, pSink, fForeground, g_key_sink);
    log_line(buf);
    log_ptr2_hr("MsctfShim: AdviseKeyEventSink", iface, pSink, S_OK);
    return S_OK;
}

static HRESULT WINAPI km_UnadviseKeyEventSink(ITfKeystrokeMgr *iface, TfClientId tid)
{
    MsctfKeystrokeWrap *k = km_from_iface(iface);
    WCHAR env[8];
    BOOL sticky = GetEnvironmentVariableW(L"AT_STICKY_KEYSINK", env, 8) > 0 && env[0] == L'1';
    (void)tid;
    log_line(sticky ? "MsctfShim: UnadviseKeyEventSink (sticky: keeping global sink)\r\n"
                    : "MsctfShim: UnadviseKeyEventSink\r\n");
    if (k->sink) {
        k->sink->lpVtbl->Release(k->sink);
        k->sink = 0;
    }
    /* When sticky, deliberately leak an AddRef on g_key_sink so keystrokes can
     * still be routed to ATOK's sink after a failed Activate tears it down. */
    if (g_key_sink && !sticky) {
        g_key_sink->lpVtbl->Release(g_key_sink);
        g_key_sink = 0;
    }
    return S_OK;
}

static HRESULT WINAPI km_GetForeground(ITfKeystrokeMgr *iface, CLSID *pclsid)
{
    MsctfKeystrokeWrap *k = km_from_iface(iface);
    char buf[192];
    if (!pclsid) return E_POINTER;
    if ((k->foreground && k->sink) || g_key_sink)
        *pclsid = CLSID_ATOK_TIP;
    else
        *pclsid = CLSID_NULL;
    wsprintfA(buf, "MsctfShim: KeystrokeMgr GetForeground clsid=%s foreground=%d sink=%p g_key_sink=%p\r\n",
              guid_string(pclsid), k->foreground, k->sink, g_key_sink);
    log_line(buf);
    return S_OK;
}

static HRESULT WINAPI km_TestKeyDown(ITfKeystrokeMgr *iface, WPARAM wParam, LPARAM lParam, BOOL *pfEaten)
{
    MsctfKeystrokeWrap *k = km_from_iface(iface);
    ITfKeyEventSink *s = k->sink ? k->sink : g_key_sink;
    char buf[160];
    if (pfEaten) *pfEaten = FALSE;
    wsprintfA(buf, "MsctfShim: km_TestKeyDown iface=%p k->sink=%p g_key_sink=%p\r\n", iface, k->sink, g_key_sink);
    log_line(buf);
    if (!s || !s->lpVtbl->OnTestKeyDown) return S_FALSE;
    wsprintfA(buf, "MsctfShim: OnTestKeyDown vk=0x%02lX sink=%p%s\r\n", (unsigned long)wParam, s, k->sink ? "" : " (global)");
    log_line(buf);
    return s->lpVtbl->OnTestKeyDown(s, g_active_context ? &g_active_context->iface : 0, wParam, lParam, pfEaten);
}

static HRESULT WINAPI km_TestKeyUp(ITfKeystrokeMgr *iface, WPARAM wParam, LPARAM lParam, BOOL *pfEaten)
{
    MsctfKeystrokeWrap *k = km_from_iface(iface);
    ITfKeyEventSink *s = k->sink ? k->sink : g_key_sink;
    char buf[128];
    if (pfEaten) *pfEaten = FALSE;
    if (!s || !s->lpVtbl->OnTestKeyUp) return S_FALSE;
    wsprintfA(buf, "MsctfShim: OnTestKeyUp vk=0x%02lX sink=%p%s\r\n", (unsigned long)wParam, s, k->sink ? "" : " (global)");
    log_line(buf);
    return s->lpVtbl->OnTestKeyUp(s, g_active_context ? &g_active_context->iface : 0, wParam, lParam, pfEaten);
}

static HRESULT WINAPI km_KeyDown(ITfKeystrokeMgr *iface, WPARAM wParam, LPARAM lParam, BOOL *pfEaten)
{
    MsctfKeystrokeWrap *k = km_from_iface(iface);
    ITfKeyEventSink *s = k->sink ? k->sink : g_key_sink;
    char buf[128];
    HRESULT hr;
    if (pfEaten) *pfEaten = FALSE;
    if (!s || !s->lpVtbl->OnKeyDown) return S_FALSE;
    msctf_append_key(wParam);
    hr = s->lpVtbl->OnKeyDown(s, g_active_context ? &g_active_context->iface : 0, wParam, lParam, pfEaten);
    wsprintfA(buf, "MsctfShim: OnKeyDown vk=0x%02lX hr=0x%08lX eaten=%d%s\r\n",
              (unsigned long)wParam, (unsigned long)hr, pfEaten ? *pfEaten : 0, k->sink ? "" : " (global)");
    log_line(buf);
    return hr;
}

static HRESULT WINAPI km_KeyUp(ITfKeystrokeMgr *iface, WPARAM wParam, LPARAM lParam, BOOL *pfEaten)
{
    MsctfKeystrokeWrap *k = km_from_iface(iface);
    ITfKeyEventSink *s = k->sink ? k->sink : g_key_sink;
    char buf[128];
    HRESULT hr;
    if (pfEaten) *pfEaten = FALSE;
    if (!s || !s->lpVtbl->OnKeyUp) return S_FALSE;
    hr = s->lpVtbl->OnKeyUp(s, g_active_context ? &g_active_context->iface : 0, wParam, lParam, pfEaten);
    wsprintfA(buf, "MsctfShim: OnKeyUp vk=0x%02lX hr=0x%08lX eaten=%d%s\r\n",
              (unsigned long)wParam, (unsigned long)hr, pfEaten ? *pfEaten : 0, k->sink ? "" : " (global)");
    log_line(buf);
    return hr;
}

static HRESULT WINAPI km_GetPreservedKey(ITfKeystrokeMgr *iface, ITfContext *pic, const TF_PRESERVEDKEY *pprekey, GUID *pguid)
{
    (void)iface; (void)pic; (void)pprekey; (void)pguid;
    return S_FALSE;
}

static HRESULT WINAPI km_IsPreservedKey(ITfKeystrokeMgr *iface, REFGUID rguid, const TF_PRESERVEDKEY *pprekey, BOOL *pfRegistered)
{
    (void)iface; (void)rguid; (void)pprekey;
    if (pfRegistered) *pfRegistered = FALSE;
    return S_FALSE;
}

static HRESULT WINAPI km_PreserveKey(ITfKeystrokeMgr *iface, TfClientId tid, REFGUID rguid, const TF_PRESERVEDKEY *prekey,
                                     const WCHAR *pchDesc, ULONG cchDesc)
{
    (void)iface; (void)tid; (void)rguid; (void)prekey; (void)pchDesc; (void)cchDesc;
    return S_OK;
}

static HRESULT WINAPI km_UnpreserveKey(ITfKeystrokeMgr *iface, REFGUID rguid, const TF_PRESERVEDKEY *pprekey)
{
    (void)iface; (void)rguid; (void)pprekey;
    return S_OK;
}

static HRESULT WINAPI km_SetPreservedKeyDescription(ITfKeystrokeMgr *iface, REFGUID rguid, const WCHAR *pchDesc, ULONG cchDesc)
{
    (void)iface; (void)rguid; (void)pchDesc; (void)cchDesc;
    return S_OK;
}

static HRESULT WINAPI km_GetPreservedKeyDescription(ITfKeystrokeMgr *iface, REFGUID rguid, BSTR *pbstrDesc)
{
    (void)iface; (void)rguid; (void)pbstrDesc;
    return S_FALSE;
}

static HRESULT WINAPI km_SimulatePreservedKey(ITfKeystrokeMgr *iface, ITfContext *pic, REFGUID rguid, BOOL *pfEaten)
{
    (void)iface; (void)pic; (void)rguid;
    if (pfEaten) *pfEaten = FALSE;
    return S_FALSE;
}

static const ITfKeystrokeMgrVtbl g_km_vtbl = {
    km_QI, km_AddRef, km_Release, km_AdviseKeyEventSink, km_UnadviseKeyEventSink, km_GetForeground,
    km_TestKeyDown, km_TestKeyUp, km_KeyDown, km_KeyUp, km_GetPreservedKey, km_IsPreservedKey,
    km_PreserveKey, km_UnpreserveKey, km_SetPreservedKeyDescription, km_GetPreservedKeyDescription,
    km_SimulatePreservedKey
};

/* Thread mgr wrapper */
static HRESULT WINAPI tm_QI(ITfThreadMgr *iface, REFIID riid, void **ppv)
{
    MsctfThreadMgrWrap *tm = tm_from_iface(iface);
    if (IsEqualIID(riid, &IID_IUnknown) || IsEqualIID(riid, &IID_ITfThreadMgr)) {
        *ppv = iface;
        iface->lpVtbl->AddRef(iface);
        return S_OK;
    }
    if (IsEqualIID(riid, &IID_LOCAL_ITfLangBarItemMgr)) {
        log_line("MsctfShim: TM QI ITfLangBarItemMgr branch enter\r\n");
        ITfLangBarItemMgr *mgr = 0;
        HRESULT hr = tm->orig->lpVtbl->QueryInterface(tm->orig, riid, (void **)&mgr);
        log_ptr_hr("MsctfShim: TM QI ITfLangBarItemMgr orig", tm->orig, hr);
        if (hr == S_OK && mgr) {
            log_line("MsctfShim: TM QI ITfLangBarItemMgr branch wrap orig\r\n");
            return wrap_langbaritemmgr_result(mgr, tm, ppv, "MsctfShim: TM QI ITfLangBarItemMgr wrapped");
        }
        log_line("MsctfShim: TM QI ITfLangBarItemMgr forcing dummy fallback\r\n");
        log_line("MsctfShim: TM QI ITfLangBarItemMgr branch return dummy\r\n");
        return wrap_langbaritemmgr_result(0, tm, ppv, "MsctfShim: TM QI ITfLangBarItemMgr dummy");
    }
    if (IsEqualIID(riid, &IID_LOCAL_ITfThreadMgrEx)) {
        BOOL created = FALSE;
        if (!tm->ex) {
            tm->ex = (MsctfThreadMgrExWrap *)HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, sizeof(MsctfThreadMgrExWrap));
            if (!tm->ex) return E_OUTOFMEMORY;
            tm->ex->iface.lpVtbl = &g_tm_ex_vtbl;
            tm->ex->refs = 1;
            tm->ex->tm = tm;
            g_last_tmex_wrap = tm->ex;
            InterlockedIncrement(&tm->refs);
            created = TRUE;
        }
        if (tm->ex) g_last_tmex_wrap = tm->ex;
        {
            char buf[192];
            wsprintfA(buf, "MsctfShim: TMEx vtbl wrap=%p ours=%p\r\n",
                      tm->ex->iface.lpVtbl, &g_tm_ex_vtbl);
            log_line(buf);
        }
        log_line(created ? "MsctfShim: TM QI ITfThreadMgrEx created\r\n" : "MsctfShim: TM QI ITfThreadMgrEx existing\r\n");
        *ppv = &tm->ex->iface;
        if (!created) {
            tm->ex->iface.lpVtbl->AddRef(&tm->ex->iface);
        }
        log_ptr_hr("MsctfShim: TM QI ITfThreadMgrEx", iface, S_OK);
        if (created) {
            ITfLangBarItemMgr *probe_mgr = 0;
            HRESULT probe_hr;
            log_line("MsctfShim: TM QI ITfThreadMgrEx self-probe langbar enter\r\n");
            probe_hr = tm->ex->iface.lpVtbl->QueryInterface(&tm->ex->iface, &IID_LOCAL_ITfLangBarItemMgr, (void **)&probe_mgr);
            log_ptr_hr("MsctfShim: TM QI ITfThreadMgrEx self-probe langbar", &tm->ex->iface, probe_hr);
            if (probe_mgr) {
                probe_mgr->lpVtbl->Release(probe_mgr);
            }
        }
        return S_OK;
    }
    if (IsEqualIID(riid, &IID_ITfSource)) {
        if (!tm->source) {
            tm->source = (MsctfSourceWrap *)HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, sizeof(MsctfSourceWrap));
            if (!tm->source) return E_OUTOFMEMORY;
            tm->source->iface.lpVtbl = &g_source_vtbl;
            tm->source->refs = 1;
            tm->source->next_cookie = 1;
            tm->source->owner = tm;
            tm->source->owner_kind = SOURCE_OWNER_THREADMGR;
        }
        *ppv = &tm->source->iface;
        tm->source->iface.lpVtbl->AddRef(&tm->source->iface);
        return S_OK;
    }
    if (IsEqualIID(riid, &IID_LOCAL_ITfSourceSingle)) {
        if (!tm->source_single) {
            tm->source_single = (MsctfSourceSingleWrap *)HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, sizeof(MsctfSourceSingleWrap));
            if (!tm->source_single) return E_OUTOFMEMORY;
            tm->source_single->iface.lpVtbl = &g_source_single_vtbl;
            tm->source_single->refs = 1;
            tm->source_single->owner = tm;
        }
        *ppv = &tm->source_single->iface;
        tm->source_single->iface.lpVtbl->AddRef(&tm->source_single->iface);
        log_ptr_hr("MsctfShim: TM QI ITfSourceSingle", iface, S_OK);
        return S_OK;
    }
    if (IsEqualIID(riid, &IID_LOCAL_ITfCompartmentMgr)) {
        ITfCompartmentMgr *mgr = 0;
        HRESULT hr = tm->orig->lpVtbl->GetGlobalCompartment(tm->orig, &mgr);
        log_ptr_hr("MsctfShim: TM QI ITfCompartmentMgr orig", tm->orig, hr);
        if (hr != S_OK || !mgr) return hr;
        return wrap_compartment_mgr_result(mgr, tm, ppv, "MsctfShim: TM QI ITfCompartmentMgr wrapped");
    }
    if (IsEqualIID(riid, &IID_LOCAL_ITfUIElementMgr)) {
        ITfUIElementMgr *mgr = 0;
        HRESULT hr = tm->orig->lpVtbl->QueryInterface(tm->orig, riid, (void **)&mgr);
        log_ptr_hr("MsctfShim: TM QI ITfUIElementMgr orig", tm->orig, hr);
        if (hr != S_OK || !mgr) return hr;
        return wrap_uielementmgr_result(mgr, ppv, "MsctfShim: TM QI ITfUIElementMgr wrapped");
    }
    if (IsEqualIID(riid, &IID_LOCAL_ITfMessagePump) ||
        IsEqualIID(riid, &IID_LOCAL_ITfCategoryMgr) ||
        IsEqualIID(riid, &IID_LOCAL_ITfDisplayAttributeMgr))
    {
        return forward_qi_with_log((IUnknown *)tm->orig, riid, ppv, "TM QI service");
    }
    if (IsEqualIID(riid, &IID_ITfKeystrokeMgr)) {
        if (!tm->km) {
            tm->km = (MsctfKeystrokeWrap *)HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, sizeof(MsctfKeystrokeWrap));
            if (!tm->km) return E_OUTOFMEMORY;
            tm->km->iface.lpVtbl = &g_km_vtbl;
            tm->km->refs = 1;
        }
        *ppv = &tm->km->iface;
        tm->km->iface.lpVtbl->AddRef(&tm->km->iface);
        return S_OK;
    }
    if (riid) {
        char buf[256];
        wsprintfA(buf, "MsctfShim: TM QI fb %s c=%d l=%d o=%d\r\n",
                  guid_string(riid),
                  IsEqualIID(riid, &IID_LOCAL_ITfCompartmentMgr),
                  IsEqualIID(riid, &IID_LOCAL_ITfLangBarItemMgr),
                  IsEqualIID(riid, &IID_LOCAL_ITfContextOwnerServices));
        log_line(buf);
    }
    return tm->orig->lpVtbl->QueryInterface(tm->orig, riid, ppv);
}

static ULONG WINAPI tm_AddRef(ITfThreadMgr *iface)
{
    return (ULONG)InterlockedIncrement(&tm_from_iface(iface)->refs);
}

static ULONG WINAPI tm_Release(ITfThreadMgr *iface)
{
    MsctfThreadMgrWrap *tm = tm_from_iface(iface);
    ULONG r = (ULONG)InterlockedDecrement(&tm->refs);
    if (!r) {
        if (tm->source) tm->source->iface.lpVtbl->Release(&tm->source->iface);
        if (tm->source_single) tm->source_single->iface.lpVtbl->Release(&tm->source_single->iface);
        if (tm->km) tm->km->iface.lpVtbl->Release(&tm->km->iface);
        if (tm->provider) tm->provider->iface.lpVtbl->Release(&tm->provider->iface);
        if (tm->advised_provider) tm->advised_provider->lpVtbl->Release(tm->advised_provider);
        if (tm->docmgr) tm->docmgr->iface.lpVtbl->Release(&tm->docmgr->iface);
        if (tm->orig) tm->orig->lpVtbl->Release(tm->orig);
        HeapFree(GetProcessHeap(), 0, tm);
    }
    return r;
}

static HRESULT WINAPI tm_Activate(ITfThreadMgr *iface, TfClientId *ptid);
static HRESULT WINAPI tm_Deactivate(ITfThreadMgr *iface);
static HRESULT WINAPI tm_CreateDocumentMgr(ITfThreadMgr *iface, ITfDocumentMgr **ppdim);
static HRESULT WINAPI tm_EnumDocumentMgrs(ITfThreadMgr *iface, IEnumTfDocumentMgrs **ppEnum);
static HRESULT WINAPI tm_GetFocus(ITfThreadMgr *iface, ITfDocumentMgr **ppdimFocus);
static HRESULT WINAPI tm_SetFocus(ITfThreadMgr *iface, ITfDocumentMgr *pdimFocus);
static HRESULT WINAPI tm_AssociateFocus(ITfThreadMgr *iface, HWND hwnd, ITfDocumentMgr *pdimNew, ITfDocumentMgr **ppdimPrev);
static HRESULT WINAPI tm_IsThreadFocus(ITfThreadMgr *iface, BOOL *pfThreadFocus);
static HRESULT WINAPI tm_GetFunctionProvider(ITfThreadMgr *iface, REFCLSID clsid, ITfFunctionProvider **ppFuncProv);
static HRESULT WINAPI tm_EnumFunctionProviders(ITfThreadMgr *iface, IEnumTfFunctionProviders **ppEnum);
static HRESULT WINAPI tm_GetGlobalCompartment(ITfThreadMgr *iface, ITfCompartmentMgr **ppCompMgr);

static inline MsctfThreadMgrWrap *tmex_tm(ITfThreadMgrEx *iface)
{
    return tmex_from_iface(iface)->tm;
}

static HRESULT WINAPI tmex_QI(ITfThreadMgrEx *iface, REFIID riid, void **ppv)
{
    if (riid) {
        char buf[192];
        wsprintfA(buf, "MsctfShim: TMEx QI entry %s\r\n", guid_string(riid));
        log_line(buf);
    }
    if (IsEqualIID(riid, &IID_IUnknown) || IsEqualIID(riid, &IID_ITfThreadMgr) || IsEqualIID(riid, &IID_LOCAL_ITfThreadMgrEx)) {
        *ppv = iface;
        iface->lpVtbl->AddRef(iface);
        return S_OK;
    }
    if (IsEqualIID(riid, &IID_LOCAL_ITfLangBarItemMgr)) {
        log_line("MsctfShim: TMEx QI ITfLangBarItemMgr branch enter\r\n");
        ITfLangBarItemMgr *mgr = 0;
        HRESULT hr = tmex_tm(iface)->orig->lpVtbl->QueryInterface(tmex_tm(iface)->orig, riid, (void **)&mgr);
        log_ptr_hr("MsctfShim: TMEx QI ITfLangBarItemMgr orig", tmex_tm(iface)->orig, hr);
        if (hr == S_OK && mgr) {
            log_line("MsctfShim: TMEx QI ITfLangBarItemMgr branch wrap orig\r\n");
            return wrap_langbaritemmgr_result(mgr, tmex_tm(iface), ppv, "MsctfShim: TMEx QI ITfLangBarItemMgr wrapped");
        }
        log_line("MsctfShim: TMEx QI ITfLangBarItemMgr forcing dummy fallback\r\n");
        log_line("MsctfShim: TMEx QI ITfLangBarItemMgr branch return dummy\r\n");
        return wrap_langbaritemmgr_result(0, tmex_tm(iface), ppv, "MsctfShim: TMEx QI ITfLangBarItemMgr dummy");
    }
    if (IsEqualIID(riid, &IID_LOCAL_ITfUIElementMgr)) {
        ITfUIElementMgr *mgr = 0;
        HRESULT hr = tmex_tm(iface)->orig->lpVtbl->QueryInterface(tmex_tm(iface)->orig, riid, (void **)&mgr);
        log_ptr_hr("MsctfShim: TMEx QI ITfUIElementMgr orig", tmex_tm(iface)->orig, hr);
        if (hr != S_OK || !mgr) return hr;
        return wrap_uielementmgr_result(mgr, ppv, "MsctfShim: TMEx QI ITfUIElementMgr wrapped");
    }
    if (IsEqualIID(riid, &IID_LOCAL_ITfMessagePump) ||
        IsEqualIID(riid, &IID_LOCAL_ITfCategoryMgr) ||
        IsEqualIID(riid, &IID_LOCAL_ITfDisplayAttributeMgr) ||
        IsEqualIID(riid, &IID_LOCAL_ITfContextOwnerServices))
    {
        return forward_qi_with_log((IUnknown *)tmex_tm(iface)->orig, riid, ppv, "TMEx QI service");
    }
    if (IsEqualIID(riid, &IID_LOCAL_ITfCompartmentMgr)) {
        ITfCompartmentMgr *mgr = 0;
        HRESULT hr = tmex_tm(iface)->orig->lpVtbl->GetGlobalCompartment(tmex_tm(iface)->orig, &mgr);
        log_ptr_hr("MsctfShim: TMEx QI ITfCompartmentMgr orig", tmex_tm(iface)->orig, hr);
        if (hr != S_OK || !mgr) return hr;
        return wrap_compartment_mgr_result(mgr, tmex_tm(iface), ppv, "MsctfShim: TMEx QI ITfCompartmentMgr wrapped");
    }
    return tmex_tm(iface)->orig->lpVtbl->QueryInterface(tmex_tm(iface)->orig, riid, ppv);
}

static ULONG WINAPI tmex_AddRef(ITfThreadMgrEx *iface)
{
    return (ULONG)InterlockedIncrement(&tmex_from_iface(iface)->refs);
}

static ULONG WINAPI tmex_Release(ITfThreadMgrEx *iface)
{
    MsctfThreadMgrExWrap *ex = tmex_from_iface(iface);
    ULONG r = (ULONG)InterlockedDecrement(&ex->refs);
    if (!r) {
        if (g_last_tmex_wrap == ex)
            g_last_tmex_wrap = 0;
        if (ex->tm && ex->tm->ex == ex) {
            ex->tm->ex = 0;
            tm_Release(&ex->tm->iface);
        }
        HeapFree(GetProcessHeap(), 0, ex);
    }
    return r;
}

static HRESULT WINAPI tmex_Activate(ITfThreadMgrEx *iface, TfClientId *ptid)
{
    return tm_Activate(&tmex_tm(iface)->iface, ptid);
}

static HRESULT WINAPI tmex_Deactivate(ITfThreadMgrEx *iface)
{
    return tm_Deactivate(&tmex_tm(iface)->iface);
}

static HRESULT WINAPI tmex_CreateDocumentMgr(ITfThreadMgrEx *iface, ITfDocumentMgr **ppdim)
{
    return tm_CreateDocumentMgr(&tmex_tm(iface)->iface, ppdim);
}

static HRESULT WINAPI tmex_EnumDocumentMgrs(ITfThreadMgrEx *iface, IEnumTfDocumentMgrs **ppEnum)
{
    return tm_EnumDocumentMgrs(&tmex_tm(iface)->iface, ppEnum);
}

static HRESULT WINAPI tmex_GetFocus(ITfThreadMgrEx *iface, ITfDocumentMgr **ppdimFocus)
{
    return tm_GetFocus(&tmex_tm(iface)->iface, ppdimFocus);
}

static HRESULT WINAPI tmex_SetFocus(ITfThreadMgrEx *iface, ITfDocumentMgr *pdimFocus)
{
    return tm_SetFocus(&tmex_tm(iface)->iface, pdimFocus);
}

static HRESULT WINAPI tmex_AssociateFocus(ITfThreadMgrEx *iface, HWND hwnd, ITfDocumentMgr *pdimNew, ITfDocumentMgr **ppdimPrev)
{
    return tm_AssociateFocus(&tmex_tm(iface)->iface, hwnd, pdimNew, ppdimPrev);
}

static HRESULT WINAPI tmex_IsThreadFocus(ITfThreadMgrEx *iface, BOOL *pfThreadFocus)
{
    return tm_IsThreadFocus(&tmex_tm(iface)->iface, pfThreadFocus);
}

static HRESULT WINAPI tmex_GetFunctionProvider(ITfThreadMgrEx *iface, REFCLSID clsid, ITfFunctionProvider **ppFuncProv)
{
    return tm_GetFunctionProvider(&tmex_tm(iface)->iface, clsid, ppFuncProv);
}

static HRESULT WINAPI tmex_EnumFunctionProviders(ITfThreadMgrEx *iface, IEnumTfFunctionProviders **ppEnum)
{
    return tm_EnumFunctionProviders(&tmex_tm(iface)->iface, ppEnum);
}

static HRESULT WINAPI tmex_GetGlobalCompartment(ITfThreadMgrEx *iface, ITfCompartmentMgr **ppCompMgr)
{
    return tm_GetGlobalCompartment(&tmex_tm(iface)->iface, ppCompMgr);
}

static HRESULT WINAPI tmex_ActivateEx(ITfThreadMgrEx *iface, TfClientId *ptid, DWORD flags)
{
    (void)flags;
    return tm_Activate(&tmex_tm(iface)->iface, ptid);
}

static HRESULT WINAPI tmex_GetActiveFlags(ITfThreadMgrEx *iface, DWORD *flags)
{
    if (flags) *flags = 0;
    return S_OK;
}

static const ITfThreadMgrExVtbl g_tm_ex_vtbl = {
    tmex_QI, tmex_AddRef, tmex_Release,
    tmex_Activate, tmex_Deactivate, tmex_CreateDocumentMgr, tmex_EnumDocumentMgrs,
    tmex_GetFocus, tmex_SetFocus, tmex_AssociateFocus, tmex_IsThreadFocus,
    tmex_GetFunctionProvider, tmex_EnumFunctionProviders, tmex_GetGlobalCompartment,
    tmex_ActivateEx, tmex_GetActiveFlags
};

static HRESULT WINAPI tm_Activate(ITfThreadMgr *iface, TfClientId *ptid)
{
    MsctfThreadMgrWrap *tm = tm_from_iface(iface);
    HRESULT hr = tm->orig->lpVtbl->Activate(tm->orig, ptid);
    if (hr != S_OK) {
        log_hr("MsctfShim: TM Activate fallback", hr);
        hr = S_OK;
    }
    if (ptid) tm->client_id = *ptid;
    tm->active = TRUE;
    g_active_tm = tm;
    msctf_try_activate_pending_atok_tip(tm);
    return hr;
}

static HRESULT WINAPI tm_Deactivate(ITfThreadMgr *iface)
{
    MsctfThreadMgrWrap *tm = tm_from_iface(iface);
    if (g_active_tm == tm)
        g_active_tm = 0;
    tm->active = FALSE;
    return tm->orig->lpVtbl->Deactivate(tm->orig);
}

static HRESULT WINAPI tm_CreateDocumentMgr(ITfThreadMgr *iface, ITfDocumentMgr **ppdim)
{
    MsctfThreadMgrWrap *tm = tm_from_iface(iface);
    ITfDocumentMgr *orig = 0;
    HRESULT hr;

    if (!ppdim) return E_POINTER;
    hr = tm->orig->lpVtbl->CreateDocumentMgr(tm->orig, &orig);
    log_ptr2_hr("MsctfShim: TM CreateDocumentMgr", tm->orig, orig, hr);
    if (hr != S_OK || !orig) return hr;
    tm->docmgr = make_docmgr(orig);
    if (!tm->docmgr) {
        orig->lpVtbl->Release(orig);
        return E_OUTOFMEMORY;
    }
    *ppdim = &tm->docmgr->iface;
    log_ptr2_hr("MsctfShim: TM CreateDocumentMgr wrapped", orig, *ppdim, S_OK);
    return S_OK;
}

static HRESULT WINAPI tm_EnumDocumentMgrs(ITfThreadMgr *iface, IEnumTfDocumentMgrs **ppEnum)
{
    return tm_from_iface(iface)->orig->lpVtbl->EnumDocumentMgrs(tm_from_iface(iface)->orig, ppEnum);
}

static HRESULT WINAPI tm_GetFocus(ITfThreadMgr *iface, ITfDocumentMgr **ppdimFocus)
{
    MsctfThreadMgrWrap *tm = tm_from_iface(iface);
    ITfDocumentMgr *orig = 0;
    HRESULT hr;
    if (!ppdimFocus) return E_POINTER;
    hr = tm->orig->lpVtbl->GetFocus(tm->orig, &orig);
    if (hr != S_OK || !orig) return hr;
    if (tm->docmgr && tm->docmgr->orig == orig) {
        *ppdimFocus = &tm->docmgr->iface;
        (*ppdimFocus)->lpVtbl->AddRef(*ppdimFocus);
        orig->lpVtbl->Release(orig);
        return S_OK;
    }
    *ppdimFocus = orig;
    return S_OK;
}

static HRESULT WINAPI tm_SetFocus(ITfThreadMgr *iface, ITfDocumentMgr *pdimFocus)
{
    MsctfThreadMgrWrap *tm = tm_from_iface(iface);
    ITfDocumentMgr *prev = 0;
    ITfDocumentMgr *wrapped_prev = 0;
    ITfDocumentMgr *wrapped_focus = pdimFocus;
    ITfThreadMgrEventSink *event_sink = 0;
    BOOL call_sink = FALSE;
    log_ptr2_hr("MsctfShim: TM SetFocus", tm->orig, pdimFocus, S_OK);
    if (pdimFocus && pdimFocus->lpVtbl == &g_docmgr_vtbl) pdimFocus = docmgr_from_iface(pdimFocus)->orig;
    if (tm->source) {
        event_sink = (ITfThreadMgrEventSink *)source_find_sink(tm->source, &IID_LOCAL_ITfThreadMgrEventSink);
    }
    if (event_sink) {
        call_sink = TRUE;
        tm->orig->lpVtbl->GetFocus(tm->orig, &prev);
        if (prev && tm->docmgr && tm->docmgr->orig == prev) {
            wrapped_prev = &tm->docmgr->iface;
        } else {
            wrapped_prev = prev;
        }
    }
    {
        HRESULT hr = tm->orig->lpVtbl->SetFocus(tm->orig, pdimFocus);
        if (hr == S_OK && call_sink) {
            ITfThreadMgrEventSink *sink = event_sink;
            if (sink->lpVtbl && sink->lpVtbl->OnSetFocus) {
                sink->lpVtbl->OnSetFocus(sink, wrapped_focus, wrapped_prev);
            }
        }
        if (prev) prev->lpVtbl->Release(prev);
        return hr;
    }
}

static HRESULT WINAPI tm_AssociateFocus(ITfThreadMgr *iface, HWND hwnd, ITfDocumentMgr *pdimNew, ITfDocumentMgr **ppdimPrev)
{
    MsctfThreadMgrWrap *tm = tm_from_iface(iface);
    ITfDocumentMgr *orig_prev = 0;
    HRESULT hr;
    log_ptr2_hr("MsctfShim: TM AssociateFocus", tm->orig, pdimNew, S_OK);
    if (pdimNew && pdimNew->lpVtbl == &g_docmgr_vtbl) pdimNew = docmgr_from_iface(pdimNew)->orig;
    hr = tm->orig->lpVtbl->AssociateFocus(tm->orig, hwnd, pdimNew, ppdimPrev ? &orig_prev : 0);
    log_ptr2_hr("MsctfShim: TM AssociateFocus forwarded", tm->orig, pdimNew, hr);
    if (hr != S_OK) return hr;
    if (ppdimPrev) {
        if (orig_prev && tm->docmgr && tm->docmgr->orig == orig_prev) {
            *ppdimPrev = &tm->docmgr->iface;
            (*ppdimPrev)->lpVtbl->AddRef(*ppdimPrev);
            orig_prev->lpVtbl->Release(orig_prev);
            log_ptr2_hr("MsctfShim: TM AssociateFocus prev wrapped", orig_prev, *ppdimPrev, S_OK);
        } else {
            *ppdimPrev = orig_prev;
            log_ptr2_hr("MsctfShim: TM AssociateFocus prev raw", orig_prev, *ppdimPrev, S_OK);
        }
    }
    return S_OK;
}

static HRESULT WINAPI tm_IsThreadFocus(ITfThreadMgr *iface, BOOL *pfThreadFocus)
{
    /* Under headless Xvfb the Wine thread may not hold real input focus, so the
     * real msctf returns FALSE and ATOK concludes it is not the active/focused
     * IME and aborts Activate. Force TRUE: we always want ATOK to believe its
     * thread is focused. */
    HRESULT hr = tm_from_iface(iface)->orig->lpVtbl->IsThreadFocus(tm_from_iface(iface)->orig, pfThreadFocus);
    {
        char buf[96];
        wsprintfA(buf, "MsctfShim: IsThreadFocus orig=%d hr=0x%08lX -> forcing TRUE\r\n",
                  pfThreadFocus ? *pfThreadFocus : -1, (unsigned long)hr);
        log_line(buf);
    }
    if (pfThreadFocus) *pfThreadFocus = TRUE;
    return S_OK;
}

static HRESULT WINAPI tm_GetFunctionProvider(ITfThreadMgr *iface, REFCLSID clsid, ITfFunctionProvider **ppFuncProv)
{
    MsctfThreadMgrWrap *tm = tm_from_iface(iface);
    char buf[192];
    if (!ppFuncProv) return E_POINTER;
    wsprintfA(buf, "MsctfShim: TM GetFunctionProvider entry iface=%p tm=%p provider=%p\r\n",
              iface, tm, tm->provider);
    log_line(buf);
    if (clsid) {
        wsprintfA(buf, "MsctfShim: TM GetFunctionProvider clsid=%s\r\n", guid_string(clsid));
        log_line(buf);
    }
    if (tm->advised_provider) {
        *ppFuncProv = tm->advised_provider;
        (*ppFuncProv)->lpVtbl->AddRef(*ppFuncProv);
        log_ptr2_hr("MsctfShim: TM GetFunctionProvider returning advised", iface, *ppFuncProv, S_OK);
        return S_OK;
    }
    if (!tm->provider) {
        tm->provider = make_function_provider(tm, clsid);
        if (!tm->provider) return E_OUTOFMEMORY;
    } else if (clsid) {
        tm->provider->clsid = *clsid;
        tm->provider->type_guid = *clsid;
    }
    *ppFuncProv = &tm->provider->iface;
    (*ppFuncProv)->lpVtbl->AddRef(*ppFuncProv);
    wsprintfA(buf, "MsctfShim: TM GetFunctionProvider returning type=%s\r\n", guid_string(&tm->provider->type_guid));
    log_line(buf);
    return S_OK;
}

static HRESULT WINAPI tm_EnumFunctionProviders(ITfThreadMgr *iface, IEnumTfFunctionProviders **ppEnum)
{
    MsctfThreadMgrWrap *tm = tm_from_iface(iface);
    char buf[160];
    if (!ppEnum) return E_POINTER;
    wsprintfA(buf, "MsctfShim: TM EnumFunctionProviders entry iface=%p tm=%p provider_enum=%p\r\n",
              iface, tm, tm->provider_enum);
    log_line(buf);
    *ppEnum = make_function_provider_enum(tm);
    if (!*ppEnum) return E_OUTOFMEMORY;
    log_ptr_hr("MsctfShim: TM EnumFunctionProviders", iface, S_OK);
    return S_OK;
}

static HRESULT WINAPI tm_GetGlobalCompartment(ITfThreadMgr *iface, ITfCompartmentMgr **ppCompMgr)
{
    MsctfThreadMgrWrap *tm = tm_from_iface(iface);
    ITfCompartmentMgr *orig = 0;
    HRESULT hr;
    if (!ppCompMgr) return E_POINTER;
    hr = tm->orig->lpVtbl->GetGlobalCompartment(tm->orig, &orig);
    log_ptr_hr("MsctfShim: TM GetGlobalCompartment orig", tm->orig, hr);
    if (hr != S_OK || !orig) return hr;
    return wrap_compartment_mgr_result(orig, tm, (void **)ppCompMgr, "MsctfShim: TM GetGlobalCompartment wrapped");
}

static const ITfThreadMgrVtbl g_tm_vtbl = {
    tm_QI, tm_AddRef, tm_Release, tm_Activate, tm_Deactivate, tm_CreateDocumentMgr, tm_EnumDocumentMgrs,
    tm_GetFocus, tm_SetFocus, tm_AssociateFocus, tm_IsThreadFocus, tm_GetFunctionProvider,
    tm_EnumFunctionProviders, tm_GetGlobalCompartment
};

static BOOL g_tmex_selfprobe_done;

__declspec(dllexport) void WINAPI MsctfShim_DumpThreadMgrExState(void)
{
    char buf[256];
    if (!g_last_tmex_wrap) {
        log_line("MsctfShim: TMEx state dump no wrap\r\n");
        return;
    }
    wsprintfA(buf, "MsctfShim: TMEx state dump wrap=%p vtbl=%p qi=%p tm=%p selfprobe=%d active=%d\r\n",
              &g_last_tmex_wrap->iface,
              g_last_tmex_wrap->iface.lpVtbl,
              g_last_tmex_wrap->iface.lpVtbl ? g_last_tmex_wrap->iface.lpVtbl->QueryInterface : 0,
              g_last_tmex_wrap->tm ? &g_last_tmex_wrap->tm->iface : 0,
              g_tmex_selfprobe_done ? 1 : 0,
              g_last_tmex_wrap->tm && g_last_tmex_wrap->tm->ex == g_last_tmex_wrap ? 1 : 0);
    log_line(buf);
}

static ITfThreadMgr *wrap_thread_mgr(ITfThreadMgr *orig)
{
    MsctfThreadMgrWrap *tm = (MsctfThreadMgrWrap *)HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, sizeof(*tm));
    char buf[160];
    if (!tm) return 0;
    tm->iface.lpVtbl = &g_tm_vtbl;
    tm->refs = 1;
    tm->orig = orig;
    wsprintfA(buf, "MsctfShim: wrap_thread_mgr orig=%p wrap=%p\r\n", orig, &tm->iface);
    log_line(buf);
    wsprintfA(buf, "MsctfShim: g_tm_ex_vtbl=%p\r\n", &g_tm_ex_vtbl);
    log_line(buf);
    if (!g_tmex_selfprobe_done) {
        ITfThreadMgrEx *tmex = 0;
        HRESULT hr = tm->iface.lpVtbl->QueryInterface(&tm->iface, &IID_LOCAL_ITfThreadMgrEx, (void **)&tmex);
        log_ptr_hr("MsctfShim: wrap_thread_mgr self-probe ITfThreadMgrEx", &tm->iface, hr);
        if (hr == S_OK && tmex) {
            ITfLangBarItemMgr *mgr = 0;
            HRESULT mgr_hr;
            g_tmex_selfprobe_done = TRUE;
            mgr_hr = tmex->lpVtbl->QueryInterface(tmex, &IID_LOCAL_ITfLangBarItemMgr, (void **)&mgr);
            log_ptr_hr("MsctfShim: wrap_thread_mgr self-probe langbar", tmex, mgr_hr);
            if (mgr) {
                mgr->lpVtbl->Release(mgr);
            }
            tmex->lpVtbl->Release(tmex);
        }
    }
    return &tm->iface;
}

static ITfInputProcessorProfiles *wrap_profiles(ITfInputProcessorProfiles *orig)
{
    MsctfProfilesWrap *p = (MsctfProfilesWrap *)HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, sizeof(*p));
    if (!p) return 0;
    p->iface.lpVtbl = &g_profiles_vtbl;
    p->refs = 1;
    p->orig = orig;
    p->current_lang = LANGID_JA;
    return &p->iface;
}

__declspec(dllexport) HRESULT WINAPI TF_CreateThreadMgr(ITfThreadMgr **ppThreadMgr)
{
    ITfThreadMgr *orig;
    HRESULT hr;

    ensure_helper();
    ensure_categorymgr_registration();
    if (!pTF_CreateThreadMgr) return E_FAIL;
    hr = pTF_CreateThreadMgr(&orig);
    if (hr != S_OK || !orig) return hr;
    *ppThreadMgr = wrap_thread_mgr(orig);
    if (!*ppThreadMgr) {
        orig->lpVtbl->Release(orig);
        return E_OUTOFMEMORY;
    }
    return S_OK;
}

__declspec(dllexport) HRESULT WINAPI TF_CreateInputProcessorProfiles(ITfInputProcessorProfiles **ppProfiles)
{
    ITfInputProcessorProfiles *orig;
    HRESULT hr;

    ensure_helper();
    ensure_categorymgr_registration();
    if (!pTF_CreateInputProcessorProfiles) return E_FAIL;
    hr = pTF_CreateInputProcessorProfiles(&orig);
    if (hr != S_OK || !orig) return hr;
    *ppProfiles = wrap_profiles(orig);
    if (!*ppProfiles) {
        orig->lpVtbl->Release(orig);
        return E_OUTOFMEMORY;
    }
    return S_OK;
}

__declspec(dllexport) HRESULT WINAPI TF_CreateLangBarItemMgr(ITfLangBarItemMgr **ppMgr)
{
    log_line("MsctfShim: TF_CreateLangBarItemMgr shim\r\n");
    if (!ppMgr) return E_POINTER;
    return wrap_langbaritemmgr_result(0, g_active_tm, (void **)ppMgr,
                                      "MsctfShim: TF_CreateLangBarItemMgr dummy");
}

__declspec(dllexport) HRESULT WINAPI DllGetClassObject(REFCLSID rclsid, REFIID riid, void **ppv)
{
    char buf[192];

    ensure_helper();
    wsprintfA(buf, "MsctfShim: DllGetClassObject clsid=%s riid=%s\r\n",
              guid_string(rclsid),
              iid_name(riid) ? iid_name(riid) : guid_string(riid));
    log_line(buf);
    if (IsEqualCLSID(rclsid, &CLSID_LOCAL_TF_CategoryMgr)) {
        log_line("MsctfShim: DllGetClassObject returning CategoryMgr factory\r\n");
        return create_factory(rclsid, riid, ppv);
    }
    if (!pDllGetClassObject) return CLASS_E_CLASSNOTAVAILABLE;
    return pDllGetClassObject(rclsid, riid, ppv);
}

__declspec(dllexport) HRESULT WINAPI DllCanUnloadNow(void)
{
    ensure_helper();
    if (!pDllCanUnloadNow) return S_FALSE;
    return pDllCanUnloadNow();
}

/* Clear the active context's composition buffer + state so the resident runtime
 * can drive many independent tests without a restart. Reconversion/range reads
 * are served from comp_text, so clearing it gives each scripted test a clean
 * slate (the ACP store accumulates separately and is not read by the probes). */
__declspec(dllexport) void WINAPI MsctfShim_Reset(void)
{
    MsctfContextWrap *c = g_active_context;
    if (!c) {
        log_line("MsctfShim: Reset no active context\r\n");
        return;
    }
    c->comp_text[0] = 0;
    c->comp_len = 0;
    c->comp_active = FALSE;
    sync_context_ranges(c);
    log_line("MsctfShim: Reset comp cleared\r\n");
}

/* Copy the active context's current composition text (the converted top-1 after a
 * henkan VK_CONVERT) into out; returns the length. The fast `henkan` path reads the
 * result straight from here — no commit, no edit session, no textstore round-trip. */
__declspec(dllexport) int WINAPI MsctfShim_GetCompText(WCHAR *out, int max)
{
    MsctfContextWrap *c = g_active_context;
    int n, i;
    if (!out || max <= 0) return 0;
    if (!c) { out[0] = 0; return 0; }
    n = (int)c->comp_len;
    if (n > max - 1) n = max - 1;
    for (i = 0; i < n; i++) out[i] = c->comp_text[i];
    out[n] = 0;
    return n;
}

__declspec(dllexport) void WINAPI MsctfShim_AppendChar(WCHAR ch)
{
    char buf[96];
    if (ch == 0xFFFF) {
        if (!g_last_tmex_wrap) {
            log_line("MsctfShim: AppendChar TMEx dump no wrap\r\n");
            return;
        }
        wsprintfA(buf, "MsctfShim: AppendChar TMEx dump wrap=%p vtbl=%p qi=%p tm=%p selfprobe=%d active=%d\r\n",
                  &g_last_tmex_wrap->iface,
                  g_last_tmex_wrap->iface.lpVtbl,
                  g_last_tmex_wrap->iface.lpVtbl ? g_last_tmex_wrap->iface.lpVtbl->QueryInterface : 0,
                  g_last_tmex_wrap->tm ? &g_last_tmex_wrap->tm->iface : 0,
                  g_tmex_selfprobe_done ? 1 : 0,
                  g_last_tmex_wrap->tm && g_last_tmex_wrap->tm->ex == g_last_tmex_wrap ? 1 : 0);
        log_line(buf);
        return;
    }
    wsprintfA(buf, "MsctfShim: AppendChar U+%04lX\r\n", (unsigned long)ch);
    log_line(buf);
    sync_active_context_text(ch);
}

BOOL WINAPI DllMain(HINSTANCE instance, DWORD reason, LPVOID reserved)
{
    if (reason == DLL_PROCESS_ATTACH) {
        g_instance = instance;
    }
    (void)reserved;
    return TRUE;
}
