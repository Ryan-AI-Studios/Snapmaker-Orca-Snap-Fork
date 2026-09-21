#include "SlotRemapDialog.hpp"

#include "GUI.hpp"
#include "I18N.hpp"
#include "wxExtensions.hpp"

#include <wx/button.h>
#include <wx/sizer.h>
#include <wx/stattext.h>

namespace Slic3r { namespace GUI {

namespace {

wxString dest_label(size_t dest_1based, const std::vector<std::string> &dest_colours)
{
    wxString label = wxString::Format(_L("Toolhead %d"), int(dest_1based));
    if (dest_1based >= 1 && dest_1based - 1 < dest_colours.size() &&
        !dest_colours[dest_1based - 1].empty()) {
        label += "  ";
        label += wxString::FromUTF8(dest_colours[dest_1based - 1].c_str());
    }
    return label;
}

} // namespace

SlotRemapDialog::SlotRemapDialog(
    wxWindow                        *parent,
    const std::vector<unsigned int> &source_ids,
    const std::vector<std::string>  &source_colours,
    const std::vector<std::string>  &dest_colours,
    const SlotRemapMap              &initial)
    : wxDialog(parent, wxID_ANY, _L("Remap toolheads"), wxDefaultPosition, wxDefaultSize,
               wxDEFAULT_DIALOG_STYLE | wxRESIZE_BORDER)
    , m_sources(source_ids)
{
    auto *root = new wxBoxSizer(wxVERTICAL);
    root->Add(new wxStaticText(this, wxID_ANY,
                  _L("Map each source slot onto a physical ZR Ultra S toolhead. "
                     "Loaded filament colours stay on the toolheads.")),
              0, wxEXPAND | wxALL, 8);

    const int dest_n = dest_colours.empty() ? 4 : int(dest_colours.size());
    auto *grid = new wxFlexGridSizer(2, 8, 8);
    grid->AddGrowableCol(1, 1);

    for (unsigned int src : m_sources) {
        wxString src_label = wxString::Format(_L("Source slot %d"), int(src));
        if (src >= 1 && src - 1 < source_colours.size() && !source_colours[src - 1].empty()) {
            src_label += "  ";
            src_label += wxString::FromUTF8(source_colours[src - 1].c_str());
        }
        grid->Add(new wxStaticText(this, wxID_ANY, src_label), 0, wxALIGN_CENTER_VERTICAL);

        auto *choice = new wxChoice(this, wxID_ANY);
        for (int d = 1; d <= dest_n; ++d)
            choice->Append(dest_label(size_t(d), dest_colours));
        unsigned int dest = slot_remap_lookup(initial, src);
        if (dest < 1 || dest > unsigned(dest_n))
            dest = src <= unsigned(dest_n) ? src : 1;
        choice->SetSelection(int(dest) - 1);
        m_choices.push_back(choice);
        grid->Add(choice, 1, wxEXPAND);
    }
    root->Add(grid, 1, wxEXPAND | wxLEFT | wxRIGHT | wxBOTTOM, 8);

    auto *btns   = new wxBoxSizer(wxHORIZONTAL);
    auto *apply  = new wxButton(this, wxID_OK, _L("Apply"));
    auto *cancel = new wxButton(this, wxID_CANCEL, _L("Cancel"));
    btns->AddStretchSpacer();
    btns->Add(apply, 0, wxRIGHT, 8);
    btns->Add(cancel, 0);
    root->Add(btns, 0, wxEXPAND | wxALL, 8);

    SetSizerAndFit(root);
    SetMinSize(wxSize(480, 240));
}

SlotRemapMap SlotRemapDialog::result_map() const
{
    SlotRemapMap map;
    for (size_t i = 0; i < m_sources.size() && i < m_choices.size(); ++i) {
        const int sel = m_choices[i] ? m_choices[i]->GetSelection() : wxNOT_FOUND;
        if (sel == wxNOT_FOUND)
            map[m_sources[i]] = 0;
        else
            map[m_sources[i]] = static_cast<unsigned int>(sel + 1);
    }
    return map;
}

}} // namespace Slic3r::GUI
