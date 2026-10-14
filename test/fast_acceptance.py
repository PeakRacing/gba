#!/usr/bin/env python3
# Copyright PeakRacing
# Licensed under the Apache License, Version 2.0. See LICENSE for details.
"""Check individually reviewed approximations without altering strict results."""
import hashlib
import json
import pathlib
import sys


def read(path):
    return json.loads(pathlib.Path(path).read_text(encoding='utf-8-sig'))


def key(row):
    values = [row['R1'], row['R2']] if row['Suite'] == 'timer-irq' else [row['R2'], row['R3']]
    if row['Suite'] == 'multiply-long':
        values += row['Stack']
    return '|'.join(str(x) for x in [row['Suite'], row['Case'], row['TestId'], row['Subtest'], row['Label'], row['Format']] + values)


def video_hash(kind):
    return hashlib.sha256(pathlib.Path(f'test/out/video/4-{kind}.ppm').read_bytes()).hexdigest()


def main():
    policy = read('docs/fast-approximations.json')
    accepted = {row['Key']: row for row in policy['Comparisons']}
    failures = read('test/out/suite/failures.json')['Failures']
    records = []
    for row in failures:
        entry = accepted.get(key(row))
        records.append(dict(row, Status='KNOWN_APPROX' if entry else 'FAIL',
                            Reason=entry['Reason'] if entry else 'unreviewed strict difference'))
    videos = []
    for row in read('test/out/video/results.json'):
        status = 'PASS' if row['Pass'] else 'FAIL'
        if not row['Pass'] and row['Name'] == policy['Video']['Name']:
            if (row['DifferentPixels'] == policy['Video']['DifferentPixels'] and
                    video_hash('actual') == policy['Video']['ActualSHA256'] and
                    video_hash('expected') == policy['Video']['ExpectedSHA256']):
                status = 'KNOWN_APPROX'
        videos.append(dict(Name=row['Name'], Status=status, DifferentPixels=row['DifferentPixels']))
    failed = sum(x['Status'] == 'FAIL' for x in records + videos)
    result = dict(Policy='single fast core; strict results remain unchanged',
                  UnreviewedFailures=failed, Comparisons=records, Video=videos)
    pathlib.Path('test/out/fast-acceptance.json').write_text(json.dumps(result, ensure_ascii=False, indent=2)+'\n')
    print(f'Fast precision policy: {len(records)-sum(x["Status"] == "FAIL" for x in records)} reviewed comparisons, unreviewed={failed}')
    return bool(failed)


if __name__ == '__main__':
    sys.exit(main())
