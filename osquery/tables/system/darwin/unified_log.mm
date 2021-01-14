/**
 * Copyright (c) 2021-present, The osquery authors
 *
 * This source code is licensed as defined by the LICENSE file found in the
 * root directory of this source tree.
 *
 * SPDX-License-Identifier: (Apache-2.0 OR GPL-2.0-only)
 */

#include <CoreFoundation/CoreFoundation.h>
#include <Foundation/Foundation.h>
#include <OSLog/OSLog.h>

#include <osquery/core/tables.h>
#include <osquery/logger/logger.h>

#include <boost/algorithm/string/replace.hpp>

namespace ba = boost::algorithm;

namespace osquery {
namespace tables {

const std::map<ConstraintOperator, NSPredicateOperatorType> supportedOps = {
    {EQUALS, NSEqualToPredicateOperatorType},
    {GREATER_THAN, NSGreaterThanPredicateOperatorType},
    {GREATER_THAN_OR_EQUALS, NSGreaterThanOrEqualToPredicateOperatorType},
    {LESS_THAN, NSLessThanPredicateOperatorType},
    {LESS_THAN_OR_EQUALS, NSLessThanOrEqualToPredicateOperatorType},
    {LIKE, NSLikePredicateOperatorType}
};

const std::map<std::string, std::string>  columnToOSLogEntryProp {
    {"timestamp", "date"},
    {"message", "composedMessage"},
    {"storage", "storeCategory"},
    {"activity", "activityIdentifier"},
    {"process", "process"},
    {"pid", "processIdentifier"},
    {"sender", "sender"},
    {"tid", "threadIdentifier"},
    {"subsystem", "subsystem"},
    {"category", "category"}
};

const std::map<std::string, bool>  columnIsNumeric {
    {"timestamp", false},
    {"message", false},
    {"storage", true},
    {"activity", true},
    {"process", false},
    {"pid", true},
    {"sender", false},
    {"tid", true},
    {"subsystem", false},
    {"category", false}
};

std::string convertLikeExpr(const std::string &value) {
    // for the LIKE operator in NSPredicates, '*' matches 0 or more characters
    //   and '?' matches a single character
    std::string res = ba::replace_all_copy(value, "%", "*");
    ba::replace_all(res, "_", "?");
    return res;
}

void addQueryOp(NSMutableArray *preds,
                const std::string &key,
                const std::string &value,
                ConstraintOperator op) {
    if (supportedOps.count(op) > 0) {
        std::string modified_val = value;
        std::string modified_key = columnToOSLogEntryProp.at(key);
        if (op == LIKE) {
            modified_val = convertLikeExpr(value);
        }
        NSExpression *keyExp = [NSExpression expressionForKeyPath:[NSString stringWithUTF8String:modified_key.c_str()]];
        NSString *valStr = [NSString stringWithUTF8String:modified_val.c_str()];

        NSExpression *valExp = nil;
        if (key == "timestamp") {
            double provided_timestamp = [valStr doubleValue];
            valExp = [NSExpression expressionForConstantValue:[NSDate dateWithTimeIntervalSince1970: provided_timestamp]];
        } else if (columnIsNumeric.at(key)) {
            valExp = [NSExpression expressionWithFormat:@"%lld", [valStr longLongValue]];
        } else {
            valExp = [NSExpression expressionForConstantValue:valStr];
        }

        NSPredicate *pred = [NSComparisonPredicate
            predicateWithLeftExpression:keyExp
            rightExpression:valExp
            modifier:NSDirectPredicateModifier
            type:supportedOps.at(op)
            options:0];
        [preds addObject:pred];
    }
}


void genUnifiedLog(QueryContext& queryContext, QueryData &results) {

    if (@available(macOS 10.15, *)) {
    NSError *error = nil;
    OSLogStore *logstore = [OSLogStore localStoreAndReturnError:&error];
    if (error != nil) {
        NSLog(@"error getting handle to log store: %@", error);
        return;
    }

    OSLogPosition *position = nil;

    NSMutableArray *subpredicates = [[NSMutableArray alloc] init];
    if (queryContext.hasConstraint("timestamp", GREATER_THAN)) {
        auto start_time = queryContext.constraints["timestamp"].getAll(GREATER_THAN);

        double provided_timestamp = [[NSString stringWithUTF8String:start_time.begin()->c_str()] doubleValue];
        NSDate *provided_date = [NSDate dateWithTimeIntervalSince1970:provided_timestamp];

        position = [logstore positionWithDate:provided_date];
    }

    for (const auto &it : queryContext.constraints) {
        const std::string &key = it.first;
        for (const auto &constraint : it.second.getAll()) {
            addQueryOp(subpredicates, key, constraint.expr,
                       static_cast<ConstraintOperator>(constraint.op));
        }
    }

    // QueryContext.constraints doesn't list how the constraints interact with each other,
    // so we assume they're all ANDed together.
    NSPredicate *predicate = [NSCompoundPredicate andPredicateWithSubpredicates:subpredicates];

    // enumerate the entries in ascending order by timestamp
    OSLogEnumeratorOptions option = 0;

    OSLogEnumerator *enumerator = [logstore entriesEnumeratorWithOptions:option
                                                                position:position
                                                               predicate:predicate
                                                                   error:&error];
    if (error != nil) {
        NSLog(@"error enumerating entries in system log: %@", error);
        return;
    }
    for (OSLogEntryLog *entry in enumerator) {
        Row r;
        r["timestamp"] = BIGINT([[entry date] timeIntervalSince1970]);
        r["message"] = TEXT([[entry composedMessage] UTF8String]);
        r["storage"] = INTEGER([entry storeCategory]);

        if ([entry respondsToSelector:@selector(activityIdentifier)]) {
          r["activity"] = INTEGER([entry activityIdentifier]);
          r["process"] = TEXT([[entry process] UTF8String]);
          r["pid"] = INTEGER([entry processIdentifier]);
          r["sender"] = TEXT([[entry sender] UTF8String]);
          r["tid"] = INTEGER([entry threadIdentifier]);
        }

        if ([entry respondsToSelector:@selector(subsystem)]) {
          NSString *subsystem = [entry subsystem];
          if (subsystem != nil) {
            r["subsystem"] = TEXT([subsystem UTF8String]);
          }
          NSString *category = [entry category];
          if (category != nil) {
            r["category"] = TEXT([category UTF8String]);
          }
        }
        results.push_back(r);
    }
    }
}

QueryData genUnifiedLog(QueryContext& context) {
  QueryData results;
  if (@available(macOS 10.15, *)) {
  @autoreleasepool {
    genUnifiedLog(context, results);
  }
  } else {
    VLOG(1) << "OSLog framework is not available";
  }
  return results;
}

} // namespace tables
} // namespace osquery