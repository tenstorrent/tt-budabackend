# SPDX-FileCopyrightText: © 2024 Tenstorrent AI ULC

# SPDX-License-Identifier: Apache-2.0
from typing import List, Any, Dict, Iterable, Tuple, Union
from copy import deepcopy
# TODO: Uncomment when installed to IRD dockers
# from xlsxwriter import Workbook
# from xlsxwriter.utility import xl_rowcol_to_cell
import csv
import os
from logger_utils import (
    logger, 
    ExcelColors,
    ASSERT,
    COLORS,
)
from sys import stdout

# return indices of all occurences of an element in arr
def all_indices(arr, element):
    return [index for index, value in enumerate(arr) if value == element]

# convert data to int/float if possible
def try_parse_number(val):
    try:
        float_val = float(val)
        return float_val if int(float_val) != float_val else int(float_val)
    except (ValueError, TypeError):
        return val

def get_column_start_end(first_row_idx: int, last_row_idx: int, column_idx: int):
    return xl_rowcol_to_cell(row=first_row_idx, col=column_idx), xl_rowcol_to_cell(row=last_row_idx, col=column_idx)

def get_row_start_end(first_col_idx: int, last_col_idx: int, row_idx: int):
    return xl_rowcol_to_cell(row=row_idx, col=first_col_idx), xl_rowcol_to_cell(row=row_idx, col=last_col_idx)

# FIXME: remove this when xlsxwriter is installed to IRD dockers
def disable_excel_features():
    assert False, "Currently all excel features are disabled until necessary library is installed."

# generate n evenly scaled colors 
def scale_color(n):
    ASSERT(n >= 1)

    green_rgb_hex = ExcelColors.green.value
    blue_rgb_hex = ExcelColors.blue.value
    if n == 1:
        return [green_rgb_hex]
    
    green_rgb = (int(green_rgb_hex[1:3], 16), int(green_rgb_hex[3:5], 16), int(green_rgb_hex[5:], 16))
    blue_rgb = (int(blue_rgb_hex[1:3], 16), int(blue_rgb_hex[3:5], 16), int(blue_rgb_hex[5:], 16))
    
    scaled_colors = []
    
    step_size_r = (blue_rgb[0] - green_rgb[0]) / (n - 1)
    step_size_g = (blue_rgb[1] - green_rgb[1]) / (n - 1)
    step_size_b = (blue_rgb[2] - green_rgb[2]) / (n - 1)
    
    # Generate scaled colors by interpolating RGB components
    for i in range(n):
        r = hex(int(green_rgb[0] + step_size_r * i))[2:].zfill(2)
        g = hex(int(green_rgb[1] + step_size_g * i))[2:].zfill(2)
        b = hex(int(green_rgb[2] + step_size_b * i))[2:].zfill(2)
        scaled_colors.append("#" + r + g + b)
    
    return scaled_colors

class ExcelChartSeries:
    # Excel chart data should be an array of numbers and an optional name that represents the legend
    def __init__(self, x_data: List[Union[int, float]], y_data: List[Union[int, float]], name=None):
        ASSERT(len(x_data) == len(y_data))
        self.x_data = deepcopy(x_data)
        self.y_data = deepcopy(y_data)
        self.name = name

    def copy(self):
        return ExcelChartSeries(deepcopy(self.x_data), deepcopy(self.y_data), self.name)
    
    def __len__(self):
        return len(self.x_data)
    
    def __getitem__(self, index):
        return (self.x_data[index], self.y_data[index])
   
class ExcelChart:
    def __init__(self, data_series: List[ExcelChartSeries], title = "", x_axis_name = "", y_axis_name = ""):
        self.data_series = [s.copy() for s in data_series]
        self.title = title
        self.x_axis_name = x_axis_name
        self.y_axis_name = y_axis_name

    def add_data_series(self, data_series: ExcelChartSeries):
        self.data_series.append(data_series.copy())
    
    def copy(self):
        return ExcelChart([self.data_series[i].copy() for i in range(len(self.data_series))], self.title, self.x_axis_name, self.y_axis_name)
    
    def __len__(self):
        return len(self.data_series)

class PerfReportConfigs:
    '''
    PerfReportConfigs defines the formatting options of PerfReport objects.

    Member variables:
    label_sort_priority:
        Type: list of strings.
        Desc: A list of labels specifying the sorting priority for the entries. Earlier labels in the list have higher sorting priority.
        Example: If labels = ['a', 'b', 'c'], and label_sort_priority = ['c', 'b'], then the entry with a smaller 'c' value will be sorted to the front.
        If two entries have the same 'c' value, then the entry with a smaller 'b' value will be sorted to the front.
    larger_is_better_labels:
        Type: set of strings. 
        Desc: A set of labels, where larger values are considered better for sorting and highlighting. 
        Example: If labels = ['a', 'b', 'c'], and label_sort_priority = ['c', 'b'], and larger_is_better_labels = {'c'}, then entries
            with larger 'c' values will be sorted to the front, and highlighted with a greener color.
    labels_to_highlight_single_color: 
        Type: set of strings.
        Desc: A set of labels (columns) to highlight with a single color in the report. This variable only affects excel generation.
    labels_to_highlight_graded_color: 
        Type: set of strings.
        Desc: A set of labels (columns) to highlight with graded colors based on values in the report. If the label is not in larger_is_better_labels,
            then smaller entries will be highlighted green while larger entries will be highlighted red. If the label is in larger_is_better_labels, 
            then smaller entries will be highlighted red and larger entries will be highlighted green. This variable only affects excel generation.
    labels_to_bold: 
        Type: set of strings.
        Desc: A set of labels (columns) to bold font in the report. This variable only affects excel generation.
    labels_to_separate
        Type: dictionary with strings as keys and set of integers as values.
        Desc: A dictionary containing labels and corresponding occurrences to separate in the report. Separation occurs by adding a blank column
            to the left of the label. This variable only affects excel generation. An empty set separates all occurrences of that label.
        Example: If labels = ['a', 'b', 'c', 'a', 'b', 'a'], and labels_to_separate = {'a': {2, 3}, 'b': {}}, then when PerfReport is dumped into an 
            excel file, all columns that have label 'b' will be separated, and the columns that contain the 2nd and 3rd occurence of label 'a' will be separated. 
            The resulting columns in the report will thus look like: ['a', blank_column, 'b', 'c', blank_column, 'a', blank_column, 'b', blank_column, 'a']
    labels_to_merge_cells
        Type: set of strings.
        Desc: A set of strings where empty cells should be merged to the previous non-empty cell. This variable only afftects excel generation.
    freeze_pane
        Type: string.
        Desc: A cell that defines which rows/cols should be frozen. This variable only affects excel generation.
        Example: If freeze_pane = A1, then the first row will be frozen. If freeze_pane = C2, then the first 2 columns and the first row will be frozen.
    excel_chart_label
        Type: string
        Desc: The label under which excel charts will be plotted.
    '''
    def __init__(self):
        self.freeze_pane = ""
        self.excel_chart_label = ""
        self.label_sort_priority = list()
        self.larger_is_better_labels = set()
        self.labels_to_highlight_single_color = set()
        self.labels_to_highlight_graded_color = set()
        self.labels_to_bold = set()
        self.labels_to_separate = dict()
        self.labels_to_merge_cells = set()

    def set_configs(
        self,
        freeze_pane = "",
        excel_chart_label = "",
        label_sort_priority = [],
        larger_is_better_labels = [],
        labels_to_highlight_graded_color = [],
        labels_to_highlight_single_color = [],
        labels_to_bold = [],
        labels_to_separate = {},
        labels_to_merge_cells = [],
    ):
        self.set_freeze_pane(freeze_pane)
        self.set_excel_chart_label(excel_chart_label)
        self.add_label_sort_priority(label_sort_priority)
        self.add_larger_is_better_labels(larger_is_better_labels)
        self.add_graded_highlight_labels(labels_to_highlight_graded_color)
        self.add_single_highlight_labels(labels_to_highlight_single_color)
        self.add_labels_to_bold(labels_to_bold)
        self.add_labels_to_separate(labels_to_separate)
        self.add_labels_to_merge_cells(labels_to_merge_cells)

    def set_freeze_pane(self, freeze_pane: str):
        self.freeze_pane = freeze_pane

    def set_excel_chart_label(self, excel_chart_label: str):
        self.excel_chart_label = excel_chart_label

    def add_label_sort_priority(self, labels: Iterable[str]):
        self.label_sort_priority += list(deepcopy(labels))

    def add_larger_is_better_labels(self, labels: Iterable[str]):
        self.larger_is_better_labels.update(deepcopy(labels))

    def add_graded_highlight_labels(self, labels: Iterable[str]):
        self.labels_to_highlight_graded_color.update(deepcopy(labels))
    
    def add_single_highlight_labels(self, labels: Iterable[str]):
        self.labels_to_highlight_single_color.update(deepcopy(labels))

    def add_labels_to_bold(self, labels: Iterable[str]):
        self.labels_to_bold.update(deepcopy(labels))

    def add_labels_to_separate(self, label_indices: Dict[str, List[int]]):
        label_indices_copy = deepcopy(label_indices)
        for label, indices in label_indices_copy.items():
            if len(indices) == 0:
                self.labels_to_separate[label] = set()
            elif label in self.labels_to_separate:
                if len(self.labels_to_separate[label]) != 0:
                    self.labels_to_separate[label].update(indices)
            else:
                self.labels_to_separate[label] = set(indices)
    
    def add_labels_to_merge_cells(self, labels_to_merge_cells: Iterable[str]):
        self.labels_to_merge_cells.update(deepcopy(labels_to_merge_cells))

    def to_dict(self):
        return {
            "freeze_pane": self.freeze_pane,
            "excel_chart_label": self.excel_chart_label,
            "label_sort_priority": deepcopy(self.label_sort_priority),
            "larger_is_better_labels": deepcopy(self.larger_is_better_labels),
            "labels_to_highlight_graded_color": deepcopy(self.labels_to_highlight_graded_color),
            "labels_to_highlight_single_color": deepcopy(self.labels_to_highlight_single_color),
            "labels_to_bold": deepcopy(self.labels_to_bold),
            "labels_to_separate": deepcopy(self.labels_to_separate),
            "labels_to_merge_cells": deepcopy(self.labels_to_merge_cells),
        }
    
    def print(self):
        print(self.to_dict())


class PerfReport:
    '''
    PerfReport is a wrapper class used to generate csv, excel and txt reports. Currently supports generate csv, append to csv, generate excel,
        generate txt, and pretty-print to console.

    Member Variables:
        NA_CELL: A class constant representing 'N/A', used as a placeholder for filling empty cells and null values.
        EMPTY_CELL: A class constant representing an empty cell.
        labels:
            Type: list of strings.
            Desc: Represents the column headers in the report. 
        entries: 
            Type: 2D list, inner list consists of strings and/or numbers.
            Desc: Each inner list represents a row of data with values corresponding to the labels.
        name: 
            Type: string.
            Desc: A name for the report. Will be used as the worksheet_name when dumping this report to an excel workbook.
        configs:
            Type: PerfReportConfigs object.
            Desc: Defines the formatting configs for the report, including labels to bold, highlight, sorting order etc.
        excel_charts:
            Type: Dict of tuple(int, int) to ExcelChart object
            Desc: Excel Chart objects defining the excel charts to be plotted. The int tuple defines the start row and end row inclusive that the corresponding
                excel chart should cover. 
    '''
    NA_CELL = 'N/A'
    EMPTY_CELL = ''
    NUM_ROWS_TO_FIT_CHART = 11
    CHART_WIDTH = 68
    def __init__(self, labels: List[str], initial_entries: List[List[Any]] = [], name: str = ""):
        self.name = name if name else "perf_report"
        self.labels: List[str] = list(deepcopy(labels))
        self.entries: List[List[Union[str, int, float]]] = []
        self.entry_color: List[str] = []
        self.add_entries(initial_entries)
        self.excel_charts: Dict[Tuple[int, int], ExcelChart] = {}
        self.configs = PerfReportConfigs()

    # Configure the report.
    # configs should be a map like: { "labels_to_bold": [...], "larger_is_better_labels": [...] }
    def set_configs(self, configs: Dict[str, Any]):
        self.configs.set_configs(**configs)

    # Add an entry to the report. The length of entry should be the same as the length of labels.
    # If there is a length mismatch between entries and labels, the entry will be padded/trimmed accordingly.
    def add_entry(self, entry: List[Union[str, int, float]], color: str = COLORS["RESET"]):
        entry_data = list(deepcopy(entry))
        ASSERT(len(self.labels) >= len(entry_data), f"Unexpected {len(self.labels)} labels < {len(entry_data)} elements in entry. Each entry should belong to a label")

        if len(entry_data) < len(self.labels):
            entry_data += [PerfReport.EMPTY_CELL for _ in range(0, len(self.labels) - len(entry_data))] 

        for i in range(len(entry_data)):
            if entry_data[i] is None:
                entry_data[i] = PerfReport.EMPTY_CELL
            elif isinstance(entry_data[i], (str, int, float)):
                entry_data[i] = try_parse_number(entry_data[i])
            else:
                entry_data[i] = str(entry_data[i])

        self.entries.append(entry_data)
        self.entry_color.append(color)

    def add_entries(self, entries: List[List[Union[str, int, float]]], colors: List[str] = []):
        for idx, entry in enumerate(entries):
            if idx < len(colors):
                color = colors[idx]
            else:
                color = COLORS['RESET']
            self.add_entry(entry, color)

    def set_excel_charts(self, excel_charts: Dict[Tuple[int, int], ExcelChart]):
        self.excel_charts = {}
        sorted_row_ranges = sorted(excel_charts.keys())
        for row_range in sorted_row_ranges:
            excel_chart = excel_charts[row_range]
            if len(excel_chart) > 0:
                self.excel_charts[row_range] = excel_charts[row_range].copy()

    # Add an excel chart to the report
    # The row ranges must be sorted and cannot overlap
    # TODO: Add check for overlapping row ranges
    def add_excel_charts(self, excel_charts: Dict[Tuple[int, int], ExcelChart]):
        for row_range, chart in excel_charts.items():
            self.excel_charts[row_range] = chart.copy()

        sorted_excel_charts = {}
        sorted_row_ranges = sorted(self.excel_charts.keys())
        for row_range in sorted_row_ranges:
            excel_chart = self.excel_charts[row_range]
            if len(excel_chart) > 0:
                sorted_excel_charts[row_range] = excel_chart

        self.excel_charts = sorted_excel_charts

    # Find corresponding indices of each label in self.labels
    def get_label_indices(self, labels: List[str]):
        seen_labels = set()
        existing_labels_indices = []
        for label in labels:
            if label in seen_labels or label not in self.labels:
                continue
            seen_labels.add(label)
            label_indices = all_indices(self.labels, label)
            existing_labels_indices += label_indices
        return existing_labels_indices
    
    # construct entries that have values matching labels
    def get_all_entries_with_labels(self, labels: List[str]):
        # sort indices to preserve the order of labels in the original report
        existing_labels_indices = sorted(self.get_label_indices(labels))
        existing_labels = [self.labels[index] for index in existing_labels_indices]
        all_label_entries = [[entry[label_id] for label_id in existing_labels_indices] for entry in self.entries]
        return existing_labels, all_label_entries
    
    # Sort entries based on label_sort_priority. 
    # Earlier labels in label_sort_priority have higher priority, default sort will happen if label_sort_priority is not defined
    def sort_entries(self):
        if not self.configs.label_sort_priority:
            self.entries.sort()
            return
        
        criteria = self.get_label_indices(labels=self.configs.label_sort_priority)
        def comparator(entry):
            entry_vals = []
            for i in criteria:
                value = entry[i] if (entry[i] != PerfReport.NA_CELL and entry[i] != PerfReport.EMPTY_CELL) else float('inf')
                if self.labels[i] in self.configs.larger_is_better_labels:
                    value *= -1
                entry_vals.append(value)
            return entry_vals

        self.entries.sort(key=comparator)

    def copy(self):
        new_report = PerfReport(labels=self.labels, initial_entries=self.entries)
        new_report.set_configs(
            self.configs.to_dict()
        )
        new_report.set_excel_charts(self.excel_charts)
        return new_report

    # return a new report identical to this one except all entries are sorted
    def new_report_with_entries_sorted(self):
        new_report = self.copy()
        new_report.sort_entries()
        return new_report
    
    # return a new report identical to this one except entries are cleared
    def new_empty_report_with_same_configs(self):
        new_report = self.copy()
        new_report.entries = []
        return new_report

    # Returns labels and entries that have been separated by inserting an empty column to the left
    def separate_columns(self, existing_labels: List[str], all_label_entries: List[List[Any]]):
        separated_labels = deepcopy(existing_labels)
        separated_entries = deepcopy(all_label_entries)
        
        occurrence_map = {}
        separated_indices = set()
        i = 0
        while i < len(separated_labels):
            label = separated_labels[i]
            if label in occurrence_map:
                occurrence_map[label] += 1
            else:
                occurrence_map[label] = 1
            
            label_occurrence = occurrence_map[label]

            # check if we should separate this occurence of the label column
            # if the label is the first label in the report (i == 0), then don't separate it
            if i > 0 and label in self.configs.labels_to_separate and (len(self.configs.labels_to_separate[label]) == 0 or label_occurrence in self.configs.labels_to_separate[label]):
                separated_labels.insert(i, PerfReport.EMPTY_CELL)
                for entry in separated_entries:
                    entry.insert(i, PerfReport.EMPTY_CELL)
                separated_indices.add(i)
                i += 2
            else:
                i += 1

        return separated_labels, separated_entries, separated_indices

    # An excel chart takes 11 excel rows to fit
    # Thus we need to append empty rows between excel charts so that they don't overlap
    def separate_excel_graph_rows(self, column_separated_entries: List[List[Any]], separated_labels: List[str]):
        row_col_separated_entries = deepcopy(column_separated_entries)
        row_revised_excel_charts = {}
        row_offset = 0
        for row_range, excel_chart in self.excel_charts.items():
            offset_row_range = (row_range[0] + row_offset, row_range[1] + row_offset)
            num_rows = offset_row_range[1] - offset_row_range[0] + 1
            ASSERT(num_rows > 0)
            # Currently if num_rows > num_rows_to_fit_chart, then the chart will not get inserted
            # TODO: elongate graph if num_rows > num_rows_to_fit_chart, update row_offset accordingly
            num_empty_entries_to_insert = PerfReport.NUM_ROWS_TO_FIT_CHART - num_rows
            if num_empty_entries_to_insert > 0:
                row_idx_to_insert_empty_entries = offset_row_range[1]
                empty_entries = [[PerfReport.EMPTY_CELL] * len(separated_labels) for _ in range(num_empty_entries_to_insert)]
                row_col_separated_entries[row_idx_to_insert_empty_entries:row_idx_to_insert_empty_entries] = empty_entries
                row_revised_excel_charts[offset_row_range] = excel_chart.copy()
                row_offset += num_empty_entries_to_insert

        return row_col_separated_entries, row_revised_excel_charts
    
    def bold_column(self, workbook, worksheet, column_idx: int, first_row_idx: int, last_row_idx: int):
        disable_excel_features()
        bold_format = workbook.add_format({"bold": True})
        start_cell, end_cell = get_column_start_end(first_row_idx, last_row_idx, column_idx)
        worksheet.conditional_format(f"{start_cell}:{end_cell}", {'type': 'no_errors', 'format': bold_format})

    # Apply single highlight color to column starting at first_row_idx and ending at last_row_idx inclusive
    def highlight_column(self, workbook, worksheet, column_idx: int, first_row_idx: int, last_row_idx: int):
        disable_excel_features()
        highlight_format = workbook.add_format({"bg_color": ExcelColors.blue.value})
        start_cell, end_cell = get_column_start_end(first_row_idx, last_row_idx, column_idx)
        worksheet.conditional_format(f"{start_cell}:{end_cell}", {'type': 'no_errors', 'format': highlight_format})

    # Apply graded coloring to column starting at first_row_idx and ending at last_row_idx inclusive
    # if larger is better, larger values will be green and smaller values will be red
    def add_graded_color_scale_to_column(self, worksheet, column_idx: int, first_row_idx: int, last_row_idx: int, larger_is_better=False):
        disable_excel_features()
        start_cell, end_cell = get_column_start_end(first_row_idx, last_row_idx, column_idx)
        # TODO: find way to automate mid_type, hardcoding to num for math util now
        graded_color_format = {
            'type': '3_color_scale',
            'min_color': ExcelColors.red.value if larger_is_better else ExcelColors.green.value,
            'mid_color': ExcelColors.yellow.value,
            'max_color': ExcelColors.green.value if larger_is_better else ExcelColors.red.value,
            'mid_type': 'num',
            'mid_value': 50
        }
        worksheet.conditional_format(f"{start_cell}:{end_cell}", graded_color_format)

    def merge_cells(self, workbook, worksheet, separated_labels, separated_entries):
        disable_excel_features()
        merge_cell_format = workbook.add_format({
            "align": "center",
            "valign": "vcenter",
            "border": 2,
            "bold": True
        })
        for label_id, label in enumerate(separated_labels):
            if label not in self.configs.labels_to_merge_cells:
                continue
            merge_range_start = 0
            merge_range_end = 0
            merged_datum = None
            for row_id, entry in enumerate(separated_entries):
                global_row_id = row_id + 1
                if entry[label_id] != PerfReport.EMPTY_CELL and merge_range_end == merge_range_start:
                    merged_datum = entry[label_id]
                    merge_range_start = global_row_id
                    merge_range_end = global_row_id
                elif entry[label_id] != PerfReport.EMPTY_CELL and merge_range_end > merge_range_start:
                    worksheet.merge_range(f"{xl_rowcol_to_cell(row=merge_range_start, col=label_id)}:{xl_rowcol_to_cell(row=merge_range_end, col=label_id)}", merged_datum, merge_cell_format)
                    merged_datum = entry[label_id]
                    merge_range_start = global_row_id
                    merge_range_end = global_row_id
                elif entry[label_id] == PerfReport.EMPTY_CELL and global_row_id == len(separated_entries) and merge_range_end + 1 > merge_range_start:
                    merge_range_end += 1
                    worksheet.merge_range(f"{xl_rowcol_to_cell(row=merge_range_start, col=label_id)}:{xl_rowcol_to_cell(row=merge_range_end, col=label_id)}", merged_datum, merge_cell_format)
                elif entry[label_id] == PerfReport.EMPTY_CELL:
                    merge_range_end += 1

    def insert_excel_charts_to_worksheet(self, workbook, worksheet, separated_labels: List[str], row_revised_excel_charts):
        disable_excel_features()
        # This is the column that the data for excel charts are recorded
        # Excel charts can only be plotted if the data exists in the excel worksheet
        hidden_data_column = len(separated_labels) * 10
        if not self.configs.excel_chart_label or self.configs.excel_chart_label not in separated_labels or not row_revised_excel_charts:
            return
        
        excel_chart_col_idx = separated_labels.index(self.configs.excel_chart_label)
        hidden_format = workbook.add_format({'num_format': ';;;'})
        for row_range, excel_chart in row_revised_excel_charts.items():
            chart_title = excel_chart.title
            x_axis_name: str = excel_chart.x_axis_name
            y_axis_name: str = excel_chart.y_axis_name
            excel_chart_row_idx = row_range[0]
            chart = workbook.add_chart({"type": "scatter"})
            colors = scale_color(len(excel_chart.data_series))
            for series_id, data_series in enumerate(excel_chart.data_series):
                worksheet.write_row(0, hidden_data_column, data_series.x_data, hidden_format)
                worksheet.write_row(1, hidden_data_column, data_series.y_data, hidden_format)
                x_value_range = f"={worksheet.get_name()}!{xl_rowcol_to_cell(row=0, col=hidden_data_column)}:{xl_rowcol_to_cell(row=0, col=hidden_data_column + len(data_series) - 1)}"
                y_value_range = f"={worksheet.get_name()}!{xl_rowcol_to_cell(row=1, col=hidden_data_column)}:{xl_rowcol_to_cell(row=1, col=hidden_data_column + len(data_series) - 1)}"
                hidden_data_column += len(data_series)
                chart.add_series({
                    'categories': x_value_range,  # x values
                    'values':     y_value_range,  # y values
                    'marker': {'type': 'circle', 'size': 4, 'color': colors[series_id]},  # Set marker type and size
                    'line': {'none': True},  # Remove connecting lines
                    'name': data_series.name
                })
            chart.set_title({"name": chart_title})
            chart.set_x_axis({
                    'name': x_axis_name,
                    'num_format': '#,##0',
                }
            )
            chart.set_y_axis({
                    'name': y_axis_name,
                    'num_format': '#,##0',
                }
            )
            chart_cell = xl_rowcol_to_cell(row=excel_chart_row_idx, col=excel_chart_col_idx)
            worksheet.insert_chart(chart_cell, chart)
        
    def to_table_str(self, labels: List[str] = []):
        if not labels:
            all_entries = [deepcopy(self.labels)] + deepcopy(self.entries)
        elif labels:
            existing_labels, all_label_entries = self.get_all_entries_with_labels(labels=labels)
            all_entries = [deepcopy(existing_labels)] + deepcopy(all_label_entries)

        for row in range(len(all_entries)):
            for col in range(len(all_entries[row])):
                all_entries[row][col] = "  " + str(all_entries[row][col]) + "  "

        num_columns = len(all_entries[0])

        column_widths = [max(len(str(row[i])) for row in all_entries) for i in range(num_columns)]

        table_rows = []
        separator = "".join("=" * (width + 1) for width in column_widths)
        
        for row_id, row in enumerate(all_entries):
            formatted_row = "|".join(str(item).center(width) if row_id == 0 else str(item).rjust(width) for (item, width) in zip(row, column_widths))
            table_rows.append(formatted_row)
            
            if row_id == 0:
                table_rows.append(separator)
        
        table_string = "\n".join(table_rows)
        return table_string
    
    # print table to console
    # labels_to_print contains specific labels to be printed. If not defined, all labels will be printed.
    def print(self, labels_to_print: List[str] = []):
        if not labels_to_print:
            existing_labels = self.labels
            all_label_entries = self.entries
        elif labels_to_print:
            existing_labels, all_label_entries = self.get_all_entries_with_labels(labels=labels_to_print)

        if not existing_labels:
            logger.warning("No recognized labels to print, returning...")
            return
        
        all_entries = [existing_labels] + all_label_entries
        num_columns = len(existing_labels)

        # Calculate the maximum width of each column
        column_widths = [max(len(str(row[i])) for row in all_entries) for i in range(num_columns)]

        # Print the table
        for entry_idx, entry in enumerate(all_entries):
            color = COLORS['RESET']
            if entry_idx > 0 and entry_idx - 1 < len(self.entry_color):
                color = self.entry_color[entry_idx - 1]
            
            ansi = "\033[1m" if entry_idx == 0 else "\033[0m"
            alignment = "^" if entry_idx == 0 else ">"
            print('-' * (sum(column_widths) + (3 * num_columns) + 1))  # Print horizontal line
            for i, cell in enumerate(entry):
                print(f"| {ansi}{color}{str(cell):{alignment}{column_widths[i]}}{COLORS['RESET']}\033[0m ", end="")
            print("|")  # Print vertical line
        print('-' * (sum(column_widths) + (3 * num_columns) + 1))  # Print final horizontal line
        stdout.flush()
    
    # write table string to a file (typically markdown or text file)
    def write_table_str_to_file(self, file_path: str):
        labels = deepcopy(self.labels)
        if self.configs.excel_chart_label and self.configs.excel_chart_label in labels:
            labels.remove(self.configs.excel_chart_label)
        table_str = self.to_table_str(labels=labels)
        with open(file_path, "w") as table_str_file:
            table_str_file.write(table_str)
            table_str_file.close()

    # Dump to a csv file.
    # labels_to_dump contains specific labels to be written to the csv file. If not defined, all labels will be written.
    def generate_csv(self, output_csv_path: str, labels_to_dump: List[str] = [], append: bool = False):
        if not output_csv_path.endswith(".csv"):
            logger.error(f"Invalid csv output path {output_csv_path}, aborting csv dump...")
            return
        
        if not labels_to_dump:
            existing_labels = self.labels
            all_label_entries = self.entries
        elif labels_to_dump:
            existing_labels, all_label_entries = self.get_all_entries_with_labels(labels=labels_to_dump)

        if self.configs.excel_chart_label and self.configs.excel_chart_label in existing_labels:
            labels = existing_labels.copy()
            labels.remove(self.configs.excel_chart_label)
            existing_labels, all_label_entries = self.get_all_entries_with_labels(labels=labels)
            
        if not existing_labels:
            logger.warning("No recognized labels to dump to csv, returning...")
            return
        
        write_flag = "a" if append else "w"
        with open(output_csv_path, write_flag, encoding='UTF8', newline='') as csv_file:
            csv_writer = csv.writer(csv_file)
            csv_writer.writerow(list(existing_labels))
            csv_writer.writerows(list(all_label_entries))
            csv_file.close()

    # Given a workbook, add a worksheet to the workbook.
    # The worksheet will dump data stored in self and apply formatting based on self.configs
    def generate_excel_worksheet_in_workbook(self, workbook, labels_to_dump: List[str] = []):
        disable_excel_features()
        if not labels_to_dump:
            existing_labels = deepcopy(self.labels)
            all_label_entries = deepcopy(self.entries)
        # filter labels_to_dump for labels that exist in the report and extract label values from each entry
        elif labels_to_dump:
            existing_labels, all_label_entries = self.get_all_entries_with_labels(labels=labels_to_dump)

        separated_labels, separated_entries, separated_indices = self.separate_columns(existing_labels=existing_labels, all_label_entries=all_label_entries)
        row_revised_excel_charts = None
        if self.configs.excel_chart_label and self.configs.excel_chart_label in separated_labels and self.excel_charts:
            separated_entries, row_revised_excel_charts = self.separate_excel_graph_rows(separated_entries, separated_labels)

        worksheet_name = self.name
        # Excel worksheet titles can't exceed 31 characters
        worksheet = workbook.add_worksheet(worksheet_name[:31])
        worksheet.set_default_row(height=20)
        
        if self.configs.freeze_pane:
            worksheet.freeze_panes(self.configs.freeze_pane)
            
        if self.configs.excel_chart_label and self.configs.excel_chart_label in separated_labels:
            excel_chart_col = separated_labels.index(self.configs.excel_chart_label)
            worksheet.set_column(excel_chart_col, excel_chart_col, PerfReport.CHART_WIDTH)

        total_entries = [separated_labels] + separated_entries

        # Write the data into the worksheet, set borders accordingly
        for row_idx, entry in enumerate(total_entries):
            for col_idx, datum in enumerate(entry):
                cell_format = workbook.add_format({"align": "right"})
                if col_idx == 0 or (col_idx - 1) in separated_indices:
                    cell_format.set_left(2)

                if (col_idx + 1) in separated_indices or col_idx == len(entry) - 1:
                    cell_format.set_right(2)

                if row_idx == len(total_entries) - 1 and col_idx not in separated_indices:
                    cell_format.set_bottom(2)

                if row_idx == 0 and col_idx not in separated_indices:
                    cell_format.set_bold()
                    cell_format.set_align("center")
                    cell_format.set_valign("vcenter")
                    cell_format.set_top(2)

                worksheet.write(row_idx, col_idx, datum, cell_format)


        for label_idx, label in enumerate(separated_labels):
            if label == PerfReport.EMPTY_CELL:
                continue
            column_index = label_idx
            if label in self.configs.labels_to_highlight_graded_color:
                larger_is_better = label in self.configs.larger_is_better_labels
                # + 1 to avoid highlighting labels
                self.add_graded_color_scale_to_column(worksheet=worksheet, column_idx=column_index, first_row_idx=worksheet.dim_rowmin + 1, last_row_idx=worksheet.dim_rowmax, larger_is_better=larger_is_better)
            elif label in self.configs.labels_to_highlight_single_color:
                # + 1 to avoid highlighting labels
                self.highlight_column(workbook=workbook, worksheet=worksheet, column_idx=column_index, first_row_idx=worksheet.dim_rowmin + 1, last_row_idx=worksheet.dim_rowmax)

            if label in self.configs.labels_to_bold:
                self.bold_column(workbook=workbook, worksheet=worksheet, column_idx=column_index, first_row_idx=worksheet.dim_rowmin, last_row_idx=worksheet.dim_rowmax)

        self.merge_cells(workbook, worksheet, separated_labels, separated_entries)
        
        if row_revised_excel_charts:
            self.insert_excel_charts_to_worksheet(workbook, worksheet, separated_labels, row_revised_excel_charts)
            
        worksheet.autofit()

    # Creates a new excel workbook, and creates a worksheet in the workbook.
    # labels_to_dump contains specific labels to be written to the excel file. If not defined, all labels will be written.
    def create_excel_worksheet(self, output_excel_path: str, labels_to_dump: List[str] = []):
        disable_excel_features()
        if not output_excel_path.endswith(".xlsx"):
            logger.error(f"Invalid excel output path {output_excel_path}")
            return
        
        workbook = Workbook(output_excel_path)

        self.generate_excel_worksheet_in_workbook(workbook, labels_to_dump)
        workbook.close()
    
    def __len__(self):
        return 1 + len(self.entries)
    
    def __getitem__(self, index):
        return ([self.labels] + self.entries)[index]
    
    # append a row to a csv
    @classmethod
    def append_row_to_csv(cls, csv_path: str, row):
        with open(csv_path, 'a', encoding='UTF8', newline='') as csv_file:
            csv_writer = csv.writer(csv_file)
            csv_writer.writerow(list(row))
            csv_file.close()

    @classmethod
    def append_multiple_rows_to_csv(cls, csv_path: str, rows):
        with open(csv_path, 'a', encoding='UTF8', newline='') as csv_file:
            csv_writer = csv.writer(csv_file)
            csv_writer.writerows(rows)
            csv_file.close()

    # Construct a PerfReport from existing csv file
    @classmethod
    def from_csv(cls, csv_path: str):
        if not os.path.isfile(csv_path) or not csv_path.endswith(".csv"):
            logger.error(f"Invalid csv path {csv_path}, returning an empty report instance...")
            return cls(labels=[], initial_entries=[])

        entries = []
        with open(csv_path, "r") as csv_file:
            csv_reader = csv.reader(csv_file)
            labels = list(next(csv_reader))
            for row in csv_reader:
                ASSERT(len(labels) >= len(row), f"Unexpected {len(labels)} labels < {len(row)} elements in entry. Each entry should belong to a label")

                if len(row) < len(labels):
                    row += [PerfReport.EMPTY_CELL for _ in range(0, len(labels) - len(row))]

                for i, cell in enumerate(row):
                    if cell is None or cell == PerfReport.NA_CELL or cell == PerfReport.EMPTY_CELL:
                        row[i] = PerfReport.NA_CELL
                    else:
                        row[i] = try_parse_number(cell)
                entries.append(row)

            csv_file.close()

        return cls(labels=labels, initial_entries=entries)
    
    # Generate an excel workbook with multiple worksheets
    # Each worksheet captures the data of one perf report in perf_reports
    @classmethod
    def generate_excel_workbook_multiple_reports(cls, perf_reports: List["PerfReport"], output_excel_path: str):
        disable_excel_features()
        if not output_excel_path.endswith(".xlsx"):
            logger.error(f"Invalid excel output path {output_excel_path}")
            return
        
        workbook = Workbook(output_excel_path)
        for report in perf_reports:
            report.generate_excel_worksheet_in_workbook(workbook)

        workbook.close()