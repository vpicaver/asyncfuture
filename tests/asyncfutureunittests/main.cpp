#include <QString>
#include <QtTest>
#include <TestRunner>
//#include <QtQuickTest>
//#include <XBacktrace.h>
#include "example.h"
#include "spec.h"
#include "bugtests.h"
#include "samplecode.h"
#include "cookbook.h"
#include "shieldtests.h"
#include "limitedtests.h"
#include "limitertests.h"
#include "settledtests.h"

static void waitForFinished(QThreadPool *pool)
{
    QEventLoop loop;

    while (!pool->waitForDone(10)) {
        loop.processEvents();
    }
}


int main(int argc, char *argv[])
{
//    XBacktrace::enableBacktraceLogOnUnhandledException();

    QCoreApplication app(argc, argv);

    TestRunner runner;
    runner.add<Spec>();
    runner.add<BugTests>();
    runner.add<ShieldTests>();
    runner.add<LimitedTests>();
    runner.add<LimiterTests>();
    runner.add<SettledTests>();
    runner.add<Example>();
    runner.add<SampleCode>();
    runner.add<Cookbook>();

    bool error = runner.exec(app.arguments());

    if (!error) {
        qDebug() << "All test cases passed!";
    }

    waitForFinished(QThreadPool::globalInstance());

    return error;
}
