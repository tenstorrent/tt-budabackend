# SPDX-FileCopyrightText: © 2024 Tenstorrent AI ULC

# SPDX-License-Identifier: Apache-2.0
from genericpath import exists
from typing import Dict, List, Tuple, Optional
import re
import os
import datetime
from elasticsearch import Elasticsearch
import yaml
import argparse
import csv
from ci.repo import ES_ENDPOINT, ES_LOGGER, ES_LOGGER_PASSOWRD

def read_gitlab_info():
    gitlab_info_obj = None
    # Get all the necessary data we want from the CI env variables
    test_suite = str(os.environ.get("CI_JOB_NAME"))
    commit_hash = str(os.environ.get("CI_COMMIT_SHA"))
    branch_name = str(os.environ.get("CI_COMMIT_REF_NAME"))
    pipeline_id = int(os.getenv("CI_PIPELINE_ID", 0))
    pipeline_url = str(os.environ.get("CI_PIPELINE_URL"))
    job_id = int(os.getenv("CI_JOB_ID", 0))
    job_url = str(os.environ.get("CI_JOB_URL"))
    gitlab_user_id = int(os.getenv("GITLAB_USER_ID", 0))
    gitlab_user_name = str(os.environ.get("GITLAB_USER_NAME"))
    runner_id = int(os.getenv("CI_RUNNER_ID", 0))
    runner_description = str(os.environ.get("CI_RUNNER_DESCRIPTION"))
    runner_tags = list(os.getenv("CI_RUNNER_TAGS", "").split(","))

    gitlab_info_obj = {
            "commit_hash": commit_hash,
            "branch_name": branch_name,
            "pipeline_id": pipeline_id,
            "pipeline_url": pipeline_url,
            "job_id": job_id,
            "job_url": job_url,
            "user_id": gitlab_user_id,
            "user_name": gitlab_user_name,
            "runner_id": runner_id,
            "runner_description": runner_description,
            "tags": runner_tags,
        }
    return gitlab_info_obj

if __name__ == "__main__":
    # parse arguments
    parser = argparse.ArgumentParser(description="")
    parser.add_argument("--files", nargs='+',  type=str, help="Coverage filename to parse")
    args, extra_run_args = parser.parse_known_args()

    TIMESTAMP = datetime.datetime.utcnow()

    for file in args.files:
        print (f"Parsing Coverage for {file}")
        gitlab_info = read_gitlab_info ()
        with open(file, "r") as input_fh:
            data_array = csv.DictReader(input_fh, delimiter=' ')

            for data in data_array:
                db_record = {
                    "@timestamp": TIMESTAMP,
                    "gitlab_info": gitlab_info,
                    "test_case": data,
                }

                es_log = Elasticsearch([ES_ENDPOINT], http_auth=(ES_LOGGER, ES_LOGGER_PASSOWRD))

                # Create the index (or use existing index with that name)
                if "test_name" in data:
                    INDEX_NAME = "llk-coverage-" + data["test_name"]
                    es_log.indices.create(
                        index=INDEX_NAME, ignore=400
                    )  # 400 means ignore error if index already exists
                    
                    # Insert data into the index
                    # print (f"Logging {db_record}")
                    # print (f"Logging to {INDEX_NAME}")
                    es_log.index(index=INDEX_NAME, document=db_record)
