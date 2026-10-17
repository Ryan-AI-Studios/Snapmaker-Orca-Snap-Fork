#include "SlotRemapDialog.hpp"

#include "GUI.hpp"
#include "I18N.hpp"
#include "wxExtensions.hpp"

#include "libslic3r/ZrToolheadLoadout.hpp"

#include <algorithm>

#include <wx/button.h>
#include <wx/sizer.h>
#include <wx/stattext.h>

namespace Slic3r { namespace GUI {

namespace {

wxPanel *make_colour_chip(wxWindow *parent, const std::string &hex)
{
    auto *chip = new wxPanel(parent, wxID_ANY, wxDefaultPosition, wxSize(24, 24), wxBORDER_SIMPLE);
    chip->SetMinSize(wxSize(24, 24));
    unsigned char r = 0, g = 0, b = 0;
    if (zr_loadout_parse_hex(hex, r, g, b))
        chip->SetBackgroundColour(wxColour(r, g, b));
    else
        chip->SetBackgroundColour(parent->GetBackgroundColour());
    chip->Show();
    return chip;
}

void apply_chip_hex(wxPanel *chip, const std::string &hex)
{
    if (chip == nullptr)
        return;
    unsigned char r = 0, g = 0, b = 0;
    if (zr_loadout_parse_hex(hex, r, g, b))
        chip->SetBackgroundColour(wxColour(r, g, b));
    else
        chip->SetBackgroundColour(chip->GetParent() ? chip->GetParent()->GetBackgroundColour() : *wxWHITE);
    chip->Show();
    chip->Refresh();
}

wxString dest_label(
    size_t                           dest_1based,
    const std::vector<std::string>  &dest_colours,
    const std::vector<std::string>  &dest_preset_names)
{
    wxString label = wxString::Format(_L("Toolhead %d"), int(dest_1based));
    if (dest_1based >= 1 && dest_1based - 1 < dest_preset_names.size() &&
        !dest_preset_names[dest_1based - 1].empty()) {
        label += "  ";
        label += wxString::FromUTF8(dest_preset_names[dest_1based - 1].c_str());
    }
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
    const std::vector<std::string>  &dest_preset_names,
    const SlotRemapMap              &initial)
    : wxDialog(parent, wxID_ANY, _L("Remap toolheads"), wxDefaultPosition, wxDefaultSize,
               wxDEFAULT_DIALOG_STYLE | wxRESIZE_BORDER)
    , m_sources(source_ids)
    , m_dest_colours(dest_colours)
{
    auto *root = new wxBoxSizer(wxVERTICAL);
    root->Add(new wxStaticText(this, wxID_ANY,
                  _L("Map each source slot onto a physical ZR Ultra S toolhead. "
                     "Loaded filament colours stay on the toolheads.")),
              0, wxEXPAND | wxALL, 8);

    const int dest_n = dest_colours.empty() ? 4 : int(dest_colours.size());
    auto *grid = new wxFlexGridSizer(4, 8, 8);
    grid->AddGrowableCol(2, 1);

    for (size_t row = 0; row < m_sources.size(); ++row) {
        const unsigned int src = m_sources[row];
        std::string src_hex;
        if (src >= 1 && src - 1 < source_colours.size())
            src_hex = source_colours[src - 1];
        wxPanel *src_chip = make_colour_chip(this, src_hex);
        grid->Add(src_chip, 0, wxALIGN_CENTER_VERTICAL);

        wxString src_label = wxString::Format(_L("Source slot %d"), int(src));
        if (!src_hex.empty()) {
            src_label += "  ";
            src_label += wxString::FromUTF8(src_hex.c_str());
        }
        grid->Add(new wxStaticText(this, wxID_ANY, src_label), 0, wxALIGN_CENTER_VERTICAL);

        auto *choice = new wxChoice(this, wxID_ANY);
        for (int d = 1; d <= dest_n; ++d)
            choice->Append(dest_label(size_t(d), dest_colours, dest_preset_names));
        unsigned int dest = slot_remap_lookup(initial, src);
        if (dest < 1 || dest > unsigned(dest_n))
            dest = src <= unsigned(dest_n) ? src : 1;
        choice->SetSelection(int(dest) - 1);
        m_choices.push_back(choice);
        grid->Add(choice, 1, wxEXPAND);

        std::string dest_hex;
        if (dest >= 1 && dest - 1 < dest_colours.size())
            dest_hex = dest_colours[dest - 1];
        wxPanel *dst_chip = make_colour_chip(this, dest_hex);
        m_dest_chips.push_back(dst_chip);
        grid->Add(dst_chip, 0, wxALIGN_CENTER_VERTICAL);

        choice->Bind(wxEVT_CHOICE, [this, row](wxCommandEvent &) { refresh_dest_chip(row); });
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
    SetMinSize(wxSize(560, 240));
    const wxSize sz = GetSize();
    if (sz.x < 560 || sz.y < 240)
        SetSize(wxSize(std::max(sz.x, 560), std::max(sz.y, 240)));
}

void SlotRemapDialog::refresh_dest_chip(size_t row)
{
    if (row >= m_choices.size() || row >= m_dest_chips.size())
        return;
    const int sel = m_choices[row] ? m_choices[row]->GetSelection() : wxNOT_FOUND;
    std::string hex;
    if (sel != wxNOT_FOUND && sel >= 0 && size_t(sel) < m_dest_colours.size())
        hex = m_dest_colours[size_t(sel)];
    apply_chip_hex(m_dest_chips[row], hex);
    Layout();
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
