// SPDX-FileCopyrightText: © 2024 Tenstorrent AI ULC
//
// SPDX-License-Identifier: Apache-2.0
#pragma once

#include "model/typedefs.h"

namespace pipegen2
{
    class Phase
    {
    public:
        Phase(PhaseId id, unsigned int tiles_count, bool next_phase_src_change, bool next_phase_dest_change,
              bool next_phase_auto_configured, unsigned int unrolled_iteration_index) :
            m_id(id),
            m_tiles_count(tiles_count),
            m_next_phase_src_change(next_phase_src_change),
            m_next_phase_dest_change(next_phase_dest_change),
            m_next_phase_auto_configured(next_phase_auto_configured),
            m_unrolled_iteration_index(unrolled_iteration_index)
        {
        }

        PhaseId get_id() const { return m_id; }

        unsigned int get_tiles_count() const { return m_tiles_count; }

        bool is_next_phase_src_change() const { return m_next_phase_src_change; }

        bool is_next_phase_dest_change() const { return m_next_phase_dest_change; }

        bool is_next_phase_auto_configured() const { return m_next_phase_auto_configured; }

        unsigned int get_unrolled_iteration_index() const { return m_unrolled_iteration_index; }

    private:
        // Phase ID i.e. phase number.
        PhaseId m_id;

        // Count of tiles sent/received in this phase.
        unsigned int m_tiles_count;

        // Should stream change source after this phase.
        bool m_next_phase_src_change;

        // Should stream change destination after this phase.
        bool m_next_phase_dest_change;

        // Should next phase be auto configured.
        bool m_next_phase_auto_configured;

        // Index of current (unrolled) iteration.
        unsigned int m_unrolled_iteration_index;
    };
}