#include "ChewingTextService.h"
#include <assert.h>
#include <string>
#include <libIME/Utils.h>

using namespace Chewing;
using namespace std;

TextService::TextService(void):
	showingCandidates_(false),
	candidateWindow_(NULL),
	chewingContext_(NULL) {
}

TextService::~TextService(void) {
	if(candidateWindow_)
		delete candidateWindow_;

	if(chewingContext_)
		chewing_delete(chewingContext_);
}

// virtual
void TextService::onActivate() {
	if(!chewingContext_) {
		chewingContext_ = chewing_new();
		chewing_set_maxChiSymbolLen(chewingContext_, 50);
	}
	if(!candidateWindow_) {
		candidateWindow_ = new Ime::CandidateWindow();
		candidateWindow_->create();
	}
}

// virtual
void TextService::onDeactivate() {
	if(chewingContext_) {
		chewing_delete(chewingContext_);
		chewingContext_ = NULL;
	}
	if(candidateWindow_) {
		delete candidateWindow_;
		candidateWindow_ = NULL;
	}
}

// virtual
void TextService::onFocus() {
}

// virtual
bool TextService::filterKeyDown(long key) {
	assert(chewingContext_);
	// TODO: check if we're in Chinses or English mode
	if(!isComposing()) {
		// when not composing, we only cares about Bopomopho
		// FIXME: we should check if the key is mapped to a phonetic symbol instead
		// FIXME: we need to handle Shift, Alt, and Ctrl, ...etc.
		if((key >= 'A' && key <= 'Z')
			|| (key >= '0' && key <= '9')) {
			return true;
		}
		return false;
	}
	return true;
}

// virtual
bool TextService::onKeyDown(long key, Ime::EditSession* session) {
	assert(chewingContext_);
    /*
     * FIXME: the following keys are not handled:
     * shift left		VK_LSHIFT
     * shift right		VK_RSHIFT
     * caps lock		VK_CAPITAL
     * page up			VK_PRIOR
     * page down		VK_NEXT
     * ctrl num
     * shift space
     * numlock num		VK_NUMLOCK
	 */

    if('A' <= key && key <= 'Z') {
        chewing_handle_Default(chewingContext_, key - 'A' + 'a');
    } else if ('0' <= key && key <= '9') {
        chewing_handle_Default(chewingContext_, key);
    } else {
        switch(key) {
            case VK_OEM_COMMA:
                chewing_handle_Default(chewingContext_, ',');
                break;
            case VK_OEM_MINUS:
                chewing_handle_Default(chewingContext_, '-');
                break;
            case VK_OEM_PERIOD:
                chewing_handle_Default(chewingContext_, '.');
                break;
            case VK_OEM_1:
                chewing_handle_Default(chewingContext_, ';');
                break;
            case VK_OEM_2:
                chewing_handle_Default(chewingContext_, '/');
                break;
            case VK_OEM_3:
                chewing_handle_Default(chewingContext_, '`');
                break;
            case VK_SPACE:
                chewing_handle_Space(chewingContext_);
                break;
            case VK_ESCAPE:
                chewing_handle_Esc(chewingContext_);
                break;
            case VK_RETURN:
                chewing_handle_Enter(chewingContext_);
                break;
            case VK_DELETE:
                chewing_handle_Del(chewingContext_);
                break;
            case VK_BACK:
                chewing_handle_Backspace(chewingContext_);
                break;
            case VK_UP:
                chewing_handle_Up(chewingContext_);
                break;
            case VK_DOWN:
                chewing_handle_Down(chewingContext_);
            case VK_LEFT:
                chewing_handle_Left(chewingContext_);
                break;
            case VK_RIGHT:
                chewing_handle_Right(chewingContext_);
                break;
            case VK_HOME:
                chewing_handle_Home(chewingContext_);
                break;
            case VK_END:
                chewing_handle_End(chewingContext_);
                break;
            default:
                return S_OK;
        }
    }

	if(chewing_keystroke_CheckIgnore(chewingContext_))
		return false;

	// handle candidates
	if(hasCandidates()) {
		if(!showingCandidates())
			showCandidates(session);
		else
			updateCandidates(session);
	}
	else {
		if(showingCandidates())
			hideCandidates();
	}

	// has something to commit
	if(chewing_commit_Check(chewingContext_)) {
		if(!isComposing()) // start the composition
			startComposition(session->context());

		char* buf = ::chewing_commit_String(chewingContext_);
		int len;
		wchar_t* wbuf = utf8ToUtf16(buf, &len);
		chewing_free(buf);
		// commit the text, replace currently selected text with our commit string
		setCompositionString(session, wbuf, len);
		delete []wbuf;

		if(isComposing())
			endComposition(session->context());
	}

	wstring compositionBuf;
	if(::chewing_buffer_Check(chewingContext_)) {
		char* buf = ::chewing_buffer_String(chewingContext_);
		int len;
		wchar_t* wbuf;
		if(buf) {
			wbuf = ::utf8ToUtf16(buf, &len);
			::chewing_free(buf);
			compositionBuf += wbuf;
			delete []wbuf;
		}
	}

	if(!::chewing_zuin_Check(chewingContext_)) {
		int zuinNum;
		char* buf = ::chewing_zuin_String(chewingContext_, &zuinNum);
		if(buf) {
			int len;
			wchar_t* wbuf = ::utf8ToUtf16(buf, &len);
			::chewing_free(buf);
			compositionBuf += wbuf;
			delete []wbuf;
		}
	}

	// has something in composition buffer
	if(!compositionBuf.empty()) {
		if(!isComposing()) { // start the composition
			startComposition(session->context());
		}
		setCompositionString(session, compositionBuf.c_str(), compositionBuf.length());
	}
	else { // nothing left in composition buffer, terminate composition status
		if(isComposing())
			endComposition(session->context());
	}

	return true;
}

// virtual
bool TextService::filterKeyUp(long key) {
	return false;
}

// virtual
bool TextService::onKeyUp(long key, Ime::EditSession* session) {
	return true;
}

void TextService::updateCandidates(Ime::EditSession* session) {
	assert(candidateWindow_);
	candidateWindow_->clear();

	::chewing_cand_Enumerate(chewingContext_);
	int n = ::chewing_cand_ChoicePerPage(chewingContext_);
	for(; n > 0 && ::chewing_cand_hasNext(chewingContext_); --n) {
		char* str = ::chewing_cand_String(chewingContext_);
		wchar_t* wstr = utf8ToUtf16(str);
		chewing_free(str);
		candidateWindow_->add(wstr);
		delete []wstr;
	}
	candidateWindow_->recalculateSize();
	candidateWindow_->refresh();

	RECT textRect;
	// get the position of composition area from TSF
	if(compositionRect(session, &textRect)) {
		// FIXME: where should we put the candidate window?
		candidateWindow_->move(textRect.left, textRect.bottom);
	}
}

void TextService::showCandidates(Ime::EditSession* session) {
	assert(candidateWindow_);
	updateCandidates(session);
	candidateWindow_->show();
	showingCandidates_ = true;
}

void TextService::hideCandidates() {
	candidateWindow_->hide();
	candidateWindow_->clear();
	showingCandidates_ = false;
}