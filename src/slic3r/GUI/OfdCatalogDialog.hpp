#ifndef slic3r_GUI_OfdCatalogDialog_hpp_
#define slic3r_GUI_OfdCatalogDialog_hpp_

#include "libslic3r/OfdCatalog.hpp"

#include <wx/dialog.h>
#include <wx/listbox.h>
#include <wx/spinctrl.h>
#include <wx/stattext.h>
#include <wx/textctrl.h>

#include <vector>

namespace Slic3r { namespace GUI {

class OfdCatalogDialog : public wxDialog
{
public:
    OfdCatalogDialog(wxWindow *parent, size_t physical_slot_count);

    bool             has_selection() const;
    OfdVariant       selected_variant() const;
    size_t           selected_slot() const; // 0-based

private:
    void refill_list();
    void update_preview();
    int  selected_index() const;

    std::vector<OfdVariant> m_catalog;
    std::vector<OfdVariant> m_recents;
    std::vector<OfdVariant> m_shown;
    size_t                  m_slot_count{0};

    wxTextCtrl   *m_search{nullptr};
    wxListBox    *m_list{nullptr};
    wxSpinCtrl   *m_slot{nullptr};
    wxStaticText *m_preview{nullptr};
};

}} // namespace Slic3r::GUI

#endif
