#ifndef slic3r_GUI_SlotRemapDialog_hpp_
#define slic3r_GUI_SlotRemapDialog_hpp_

#include "libslic3r/SlotRemap.hpp"

#include <wx/choice.h>
#include <wx/dialog.h>
#include <wx/panel.h>

#include <vector>

namespace Slic3r { namespace GUI {

class SlotRemapDialog : public wxDialog
{
public:
    SlotRemapDialog(
        wxWindow                         *parent,
        const std::vector<unsigned int>  &source_ids,
        const std::vector<std::string>   &source_colours,
        const std::vector<std::string>   &dest_colours,
        const std::vector<std::string>   &dest_preset_names,
        const SlotRemapMap               &initial);

    SlotRemapMap result_map() const;

private:
    void refresh_dest_chip(size_t row);

    std::vector<unsigned int> m_sources;
    std::vector<std::string>  m_dest_colours;
    std::vector<wxChoice *>   m_choices;
    std::vector<wxPanel *>    m_dest_chips;
};

}} // namespace Slic3r::GUI

#endif
