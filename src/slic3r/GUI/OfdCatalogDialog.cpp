#include "OfdCatalogDialog.hpp"

#include "GUI.hpp"
#include "GUI_App.hpp"
#include "I18N.hpp"
#include "MsgDialog.hpp"
#include "wxExtensions.hpp"

#include <wx/button.h>
#include <wx/sizer.h>

namespace Slic3r { namespace GUI {

OfdCatalogDialog::OfdCatalogDialog(wxWindow *parent, size_t physical_slot_count)
    : wxDialog(parent, wxID_ANY, _L("OFD catalog"), wxDefaultPosition, wxDefaultSize, wxDEFAULT_DIALOG_STYLE | wxRESIZE_BORDER)
    , m_slot_count(physical_slot_count)
{
    m_catalog = ofd_load_catalog(ofd_default_seed_path(), ofd_default_overlay_path());
    m_recents = ofd_load_recents_file(ofd_default_recents_path());

    auto *root = new wxBoxSizer(wxVERTICAL);

    m_search = new wxTextCtrl(this, wxID_ANY);
    m_search->SetHint(_L("Search brand, filament, or colour"));
    root->Add(m_search, 0, wxEXPAND | wxALL, 8);

    m_list = new wxListBox(this, wxID_ANY, wxDefaultPosition, wxSize(480, 280));
    root->Add(m_list, 1, wxEXPAND | wxLEFT | wxRIGHT, 8);

    auto *slot_row = new wxBoxSizer(wxHORIZONTAL);
    slot_row->Add(new wxStaticText(this, wxID_ANY, _L("Physical slot")), 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, 8);
    const int max_slot = physical_slot_count < 1 ? 1 : int(physical_slot_count);
    m_slot = new wxSpinCtrl(this, wxID_ANY, wxEmptyString, wxDefaultPosition, wxDefaultSize,
                            wxSP_ARROW_KEYS, 1, max_slot, 1);
    slot_row->Add(m_slot, 0, wxALIGN_CENTER_VERTICAL);
    root->Add(slot_row, 0, wxALL, 8);

    m_preview = new wxStaticText(this, wxID_ANY, wxEmptyString);
    root->Add(m_preview, 0, wxEXPAND | wxLEFT | wxRIGHT | wxBOTTOM, 8);

    auto *btns = new wxBoxSizer(wxHORIZONTAL);
    auto *apply  = new wxButton(this, wxID_OK, _L("Apply"));
    auto *cancel = new wxButton(this, wxID_CANCEL, _L("Cancel"));
    btns->AddStretchSpacer();
    btns->Add(apply, 0, wxRIGHT, 8);
    btns->Add(cancel, 0);
    root->Add(btns, 0, wxEXPAND | wxALL, 8);

    SetSizerAndFit(root);
    SetMinSize(wxSize(520, 420));

    m_search->Bind(wxEVT_TEXT, [this](wxCommandEvent &) { refill_list(); });
    m_list->Bind(wxEVT_LISTBOX, [this](wxCommandEvent &) { update_preview(); });
    m_list->Bind(wxEVT_LISTBOX_DCLICK, [this](wxCommandEvent &) {
        if (has_selection())
            EndModal(wxID_OK);
    });

    refill_list();
}

void OfdCatalogDialog::refill_list()
{
    const std::string needle = into_u8(m_search->GetValue());
    const OfdComposedList view = ofd_compose_list(m_recents, m_catalog, {}, needle);
    m_shown = view.matches;
    m_list->Clear();
    for (size_t i = 0; i < m_shown.size(); ++i) {
        const OfdVariant &v = m_shown[i];
        wxString label = wxString::FromUTF8(v.brand.c_str()) + " / " +
                         wxString::FromUTF8(v.filament.c_str()) + " / " +
                         wxString::FromUTF8(v.variant.c_str());
        if (i < view.recent_prefix)
            label = _L("Recent") + ": " + label;
        m_list->Append(label);
    }
    if (!m_shown.empty())
        m_list->SetSelection(0);
    update_preview();
}

void OfdCatalogDialog::update_preview()
{
    const int idx = selected_index();
    if (idx < 0) {
        m_preview->SetLabel(_L("No matching filaments."));
        return;
    }
    const OfdVariant &v = m_shown[size_t(idx)];
    wxString hexes;
    for (size_t i = 0; i < v.color_hexes.size(); ++i) {
        if (i)
            hexes += ", ";
        hexes += wxString::FromUTF8(v.color_hexes[i].c_str());
    }
    m_preview->SetLabel(wxString::Format(_L("Colour: %s"), hexes));
}

int OfdCatalogDialog::selected_index() const
{
    if (m_list == nullptr)
        return -1;
    const int sel = m_list->GetSelection();
    if (sel == wxNOT_FOUND || sel < 0 || size_t(sel) >= m_shown.size())
        return -1;
    return sel;
}

bool OfdCatalogDialog::has_selection() const { return selected_index() >= 0; }

OfdVariant OfdCatalogDialog::selected_variant() const
{
    const int idx = selected_index();
    if (idx < 0)
        return {};
    return m_shown[size_t(idx)];
}

size_t OfdCatalogDialog::selected_slot() const
{
    if (m_slot == nullptr || m_slot_count == 0)
        return 0;
    const int v = m_slot->GetValue();
    if (v < 1)
        return 0;
    return size_t(v - 1);
}

}} // namespace Slic3r::GUI
