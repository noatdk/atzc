#ifndef AT_TEXTSTORE_H
#define AT_TEXTSTORE_H

#include <windows.h>
#include <objbase.h>
#include <msctf.h>
#include <textstor.h>

#ifndef __ITfContextOwner_INTERFACE_DEFINED__
#define __ITfContextOwner_INTERFACE_DEFINED__

EXTERN_C const IID IID_ITfContextOwner;

typedef interface ITfContextOwner ITfContextOwner;
typedef struct ITfContextOwnerVtbl {
    BEGIN_INTERFACE

    HRESULT (STDMETHODCALLTYPE *QueryInterface)(ITfContextOwner *This, REFIID riid, void **ppvObject);
    ULONG (STDMETHODCALLTYPE *AddRef)(ITfContextOwner *This);
    ULONG (STDMETHODCALLTYPE *Release)(ITfContextOwner *This);

    HRESULT (STDMETHODCALLTYPE *GetACPFromPoint)(ITfContextOwner *This, const POINT *ptScreen, DWORD dwFlags, LONG *pacp);
    HRESULT (STDMETHODCALLTYPE *GetTextExt)(ITfContextOwner *This, TfEditCookie ec, LONG acpStart, LONG acpEnd, RECT *prc, BOOL *pfClipped);
    HRESULT (STDMETHODCALLTYPE *GetScreenExt)(ITfContextOwner *This, TfEditCookie ec, LONG acpStart, LONG acpEnd, RECT *prc);
    HRESULT (STDMETHODCALLTYPE *GetStatus)(ITfContextOwner *This, TS_STATUS *pdcs);
    HRESULT (STDMETHODCALLTYPE *GetWnd)(ITfContextOwner *This, HWND *phwnd);
    HRESULT (STDMETHODCALLTYPE *QueryInsert)(ITfContextOwner *This, LONG acpTestStart, LONG acpTestEnd, ULONG cch, LONG *pacpResultStart, LONG *pacpResultEnd);

    END_INTERFACE
} ITfContextOwnerVtbl;

interface ITfContextOwner {
    CONST_VTBL ITfContextOwnerVtbl *lpVtbl;
};

#define ITfContextOwner_QueryInterface(This,riid,ppvObject) (This)->lpVtbl->QueryInterface(This,riid,ppvObject)
#define ITfContextOwner_AddRef(This) (This)->lpVtbl->AddRef(This)
#define ITfContextOwner_Release(This) (This)->lpVtbl->Release(This)
#define ITfContextOwner_GetACPFromPoint(This,ptScreen,dwFlags,pacp) (This)->lpVtbl->GetACPFromPoint(This,ptScreen,dwFlags,pacp)
#define ITfContextOwner_GetTextExt(This,ec,acpStart,acpEnd,prc,pfClipped) (This)->lpVtbl->GetTextExt(This,ec,acpStart,acpEnd,prc,pfClipped)
#define ITfContextOwner_GetScreenExt(This,ec,acpStart,acpEnd,prc) (This)->lpVtbl->GetScreenExt(This,ec,acpStart,acpEnd,prc)
#define ITfContextOwner_GetStatus(This,pdcs) (This)->lpVtbl->GetStatus(This,pdcs)
#define ITfContextOwner_GetWnd(This,phwnd) (This)->lpVtbl->GetWnd(This,phwnd)
#define ITfContextOwner_QueryInsert(This,acpTestStart,acpTestEnd,cch,pacpResultStart,pacpResultEnd) (This)->lpVtbl->QueryInsert(This,acpTestStart,acpTestEnd,cch,pacpResultStart,pacpResultEnd)

#endif

IUnknown *TextStore_Create(void);
void TextStore_SetEditCookie(TfEditCookie cookie);
void TextStore_LogContent(void);
int TextStore_GetText(WCHAR *out, int max);
void TextStore_LogSelection(ITfContext *context, const char *tag);
ULONG TextStore_GetCompositionCount(void);
void TextStore_AppendChar(WCHAR ch);
void TextStore_SetStaticFlags(DWORD flags);
void TextStore_SetInterimSelection(BOOL on);

HRESULT CompositionSink_Advise(ITfContext *context);
void CompositionSink_Unadvise(ITfContext *context);
void Composition_PollContext(ITfContext *context, const char *tag);

void TextStore_SetHostWindow(HWND hwnd);

#endif
