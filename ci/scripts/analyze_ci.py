# SPDX-FileCopyrightText: © 2024 Tenstorrent AI ULC
# SPDX-License-Identifier: Apache-2.0

import sys
import os
import json
import yaml
from elasticsearch import Elasticsearch
import tabulate

# add project root to search path
project_root_path = os.path.join(os.path.dirname(__file__), "../..")
sys.path.append(project_root_path)

from third_party.confidential_keys.elastic_search import ES_ENDPOINT, ES_USERNAME, ES_PASSWORD


def print_yellow(text:str):
    """Print yellow text"""
    print("\033[93m" + text + "\033[0m")


def print_red(text:str):
    """Print red text"""
    print("\033[91m" + text + "\033[0m")


def print_green(text:str):
    """Print green text"""
    print("\033[92m" + text + "\033[0m")


def scan_test_lists(directory:str) -> dict:
    """
    Scan the test-lists in the specified directory.
    Return tests gruoped by the build key.
    """
    test_lists = {}

    if not os.path.isdir(directory):
        print(f"{directory} is not a valid directory.")
        return None

    print("\nScanning test-lists in " + directory)

    for filename in os.listdir(directory):
        file_path = os.path.join(directory, filename)
        if os.path.isfile(file_path) and filename.endswith('.yaml'):
            data = None
            with open(file_path, 'r', encoding="utf-8") as file:
                data = yaml.load(file, Loader=yaml.SafeLoader)

            test_list_name = next(iter(data))
            data = data[test_list_name]
            tags = data['tag']
            if isinstance(tags, str):
                tags = [tags]

            tests = {k:v for k,v in data.items() if isinstance(v, dict) and ('command' in v or 'multi_test' in v)}
            for tag in tags:
                if tag != "nightly":
                    continue
                test_list_key = f"{data['arch_name'].lower()}_{data['device_runner'].lower()}_{tag.lower()}"
                if test_list_key not in test_lists:
                    test_lists[test_list_key] = []
                test_lists[test_list_key].extend(tests)

    return test_lists


def sort_and_save_json(data: dict, filename:str):
    """
    Sort the dictionary and save it to a json file.
    """
    data = dict(sorted(data.items()))
    for key in data:
        data[key] = sorted(data[key])
    with open(filename, 'w', encoding="utf-8") as file:
        file.write(json.dumps(data, indent=4))


def get_es_test_list():
    """
    Get the test-list and build info from elastic search
    """
    es = Elasticsearch([ES_ENDPOINT], http_auth=(ES_USERNAME, ES_PASSWORD))

    print("\nGetting test-list from elastic search")
    print("Looking for last nightly pipeline and its builds\n")

    # Search for the latest document with "pipe_stage": "PIPELINE" and "build_id" like "buda_pipeline_nightly"
    pipeline_query = {
        "query": {
            "bool": {
                "must": [
                    {"match": {"pipe_stage": "PIPELINE"}},
                    {"wildcard": {"build_id": "buda_pipeline_nightly*"}}
                ]
            }
        },
        "sort": [{"@timestamp": {"order": "desc"}}],
        "size": 1
    }

    pipeline_res = es.search(index="spatial-ci-tick", body=pipeline_query)
    pipeline_id = pipeline_res['hits']['hits'][0]["_id"]
    pipeline = pipeline_res['hits']['hits'][0]["_source"]

    # Search for the first document with "pipe_stage": "BUILD" and "pipeline_id": pipeline_id
    build_query = {
        "query": {
            "bool": {
                "must": [
                    {"match": {"pipe_stage": "BUILD"}},
                    {"match": {"pipeline_id": pipeline_id}}
                ]
            }
        },
        "size": 10
    }

    build_res = es.search(index="spatial-ci-tick", body=build_query)

    test_lists = {}
    build_info = {}

    table = []
    for build in build_res['hits']['hits']:
        build = build['_source']
        test_lists_key = build['build_id'].split("-")[0]
        test_lists[test_lists_key] = build['test_list'].split()

        table.append({"Build": build['build_id'], "Test passed": build['test_count_passed'], "Test failed": build['test_count_failed'], "Test not run": build['test_count_not_run'], "Tests": len(test_lists[test_lists_key])})

        build_info[test_lists_key] = {
            "hostname": build['hostname'],
            "log_path": build['log_path'],
            "log_name": f"{build['device_runner']}_{build['arch_name']}_{build['tag']}",
            "repo_path": build['repo_path'],
        }

    print(f"\nLast nightly pipeline was {pipeline['build_id']} at {pipeline['@timestamp']} with id {pipeline_id}\n")
    table.append({"Build": "-", "Test passed": "-", "Test failed": "-", "Test not run": "-", "Tests": "-"})
    table.append({"Build": "TOTAL", "Test passed": pipeline['test_count_passed'], "Test failed": pipeline['test_count_failed'], "Test not run": pipeline['test_count_not_run'], "Tests": sum([len(tests) for tests in test_lists.values()])})
    print(tabulate.tabulate(table, headers="keys"))
    
    return test_lists, build_info


def compare_nightly():
    """
    Scan test-lists and compare the nightly count with the elastic search
    """
    local_test_list = scan_test_lists("ci/test-lists")
    es_test_list, build_info = get_es_test_list()

    sort_and_save_json(local_test_list, "local_test_list.json")
    sort_and_save_json(es_test_list, "es_test_list.json")

    difference = {}

    print("\nComparing local test-lists with elastic search\n")
    for key in local_test_list:
        local_count = len(local_test_list[key])
        if key not in es_test_list:
            print_red(f"Test group {key} is missing in elastic search. Missing {local_count} tests [x]\n")
            continue

        es_count = len(es_test_list[key])
        if local_count != es_count:
            print_yellow(f"Test group {key} has {local_count} tests in local test-lists and {es_count} tests in elastic search. Missing {local_count-es_count} tests [-]")
            local_set = set(local_test_list[key])
            es_set = set(es_test_list[key])
            
            delta = list(local_set - es_set)
            difference[key] = delta
            print("Missing test:")
            for test_name in delta:
                print(f"  {test_name}")

            investigate_logs(key, build_info[key])

        else:
            print_green(f"Test group {key} has {local_count} tests in both local test-lists and elastic search [✓]")
        print()
    
    sort_and_save_json(difference, "difference.json")
    print()


def investigate_logs(test_group, build_info):
    """
    Investigate the logs for the test group.
    """
    print(f"Investigating logs for {test_group}\n")

    print(f"Hostname: {build_info['hostname']}")
    print(f"ssh {build_info['hostname']} \"tail -n10 {build_info['log_path']}/ci_log_dump/{build_info['log_name']}_build.log\"")
    print(f"ssh {build_info['hostname']} \"tail -n10 {build_info['log_path']}/ci_log_dump/{build_info['log_name']}_dispatch_test.log\"")
    print()


def main():
    """
    Call with arguments:
    compare_nightly: try to find tests that didnt build or didnt dipatch by comparing the nightly test results with what is defined in test-list
    """
    compare_nightly()

    # TODO args
    # parser = argparse.ArgumentParser(description='Analyze CI')
    # parser.add_argument('action', help='Action to perform')
    # args = parser.parse_args()

    # if args.action == "compare_nightly":
    #     compare_nightly()
    # else:
    #     print("Unknown action: " + args.action)
    #     sys.exit(1)

if __name__ == "__main__":
    main()
