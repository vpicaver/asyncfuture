## AsyncFuture - Use QFuture like a Promise object
![GitHub Workflow Status](https://github.com/vpicaver/asyncfuture/actions/workflows/linux_build_and_test.yml/badge.svg)

![GitHub Workflow Status](https://github.com/vpicaver/asyncfuture/actions/workflows/windows_build_and_test.yml/badge.svg)


AsyncFuture is a small static library built with CMake: the templates live in `asyncfuture.h`, and `asyncfuture.cpp` holds the `Ledger` classes. It requires Qt 6.8 or later.

QFuture is used together with QtConcurrent to represent the result of an asynchronous computation. It is a powerful component for multi-thread programming. But its usage is limited to the result of threads, it doesn't work with the asynchronous signal emitted by QObject. And it is a bit trouble to setup the listener function via QFutureWatcher.

AsyncFuture is designed to enhance the function to offer a better way to use it for asynchronous programming. It provides a Promise object like interface. This project is inspired by AsynQt and RxCpp.

Reference Articles
------------------

1. [Multithreading Programming with Future & Promise – E-Fever – Medium](https://medium.com/e-fever/multithreading-programming-with-future-promise-2d35e13b9404)
1. [AsyncFuture Cookbook 1 — Calling QtConcurrent::mapped within the run function](https://medium.com/e-fever/asyncfuture-cookbook-1-calling-qtconcurrent-mapped-within-the-run-function-ba58d523a0ce)

Including it into your project
==============================

### CMake
    add_subdirectory(asyncfuture)
    target_link_libraries(MyLib PRIVATE asyncfuture)

CMake is the only supported build. This fork dropped the qmake (`asyncfuture.pri`) and qbs (`asyncfuture.qbs`) project files when `Ledger` gained a signal, since the library now needs moc.


Features
========

 1. Convert a signal from QObject into a QFuture object
 2. Combine multiple futures with different type into a single future object
 3. Use QFuture like a Promise object
 4. Chainable Callback - Advanced multi-threading programming model
 5. Automatic future unwrapping
 6. Full chain cancellation propagation
 7. Automatic progress accumulation

**1. Convert a signal from QObject into a QFuture object**

```c++

#include "asyncfuture.h"
using namespace AsyncFuture;

// Convert a signal from QObject into a QFuture object

QFuture<void> future = observe(timer, &QTimer::timeout).future();

/* Listen from the future without using QFutureWatcher<T>*/
observe(future).subscribe([]() {
    // onCompleted. It is invoked when the observed future is finished successfully
    qDebug() << "onCompleted";
},[]() {
    // onCanceled
    qDebug() << "onCancel";
});

```

**2. Combine multiple futures with different type into a single future object**

```c++
/* Combine multiple futures with different type into a single future */

QFuture<QImage> f1 = QtConcurrent::run(readImage, QString("image.jpg"));

QFuture<void> f2 = observe(timer, &QTimer::timeout).future();

QFuture<QImage> result = (combine() << f1 << f2).subscribe([=](){
    // Read an image but do not return before timeout
    return f1.result();
}).future();

QCOMPARE(result.progressMaximum(), 2); // Added since v0.4.1

```

**3. Use QFuture like a Promise object**

Create a QFuture then complete / cancel it by yourself.

```c++
// Complete / cancel a future on your own choice
auto d = deferred<bool>();

d.subscribe([]() {
    qDebug() << "onCompleted";
}, []() {
    qDebug() << "onCancel";
});

d.complete(true); // or d.cancel();

QCOMPARE(d.future().isFinished(), true);
QCOMPARE(d.future().isCanceled(), false);

```

Complete / cancel a future according to another future object.


```c++
// Complete / cancel a future according to another future object.

auto d = deferred<void>();

d.complete(QtConcurrent::run(timeout));

QCOMPARE(d.future().isFinished(), false);
QCOMPARE(d.future().isCanceled(), false);

```

Read a file. If timeout, cancel it.

```c++

auto timeout = observe(timer, &QTimer::timeout).future();

auto defer = deferred<QString>();

defer.complete(QtConcurrent::run(readFileworker, fileName));
defer.cancel(timeout);

return defer.future();
```

**4. Chainable Callback - Advanced multi-threading programming model**

Futures can be chained into a sequence of process. And represented by a single future object.

```c++
/* Start a thread and process its result in main thread */

QFuture<QImage> readImage(const QString& file) {

    auto readImageWorker = [=]() {
        QImage image;
        image.read(file);
        return image;
    };
    
    auto updateCache = [&](QImage image) {
        m_cache[file] = image;
        return image;
    };

    QFuture<QImage> reading = QtConcurrent::run(readImageWorker);
    return observe(reading).subscribe(updateCache).future();   
}

// Read image by a thread, when it is ready, run the updateCache function
// in the main thread.
// And it return another QFuture to represent the final result.

```

```c++
/* Start a thread and process its result in main thread, then start another thread. */

QFuture<int> f1 = QtConcurrent::mapped(input, mapFunc);

QFuture<int> f2 = observe(f1).subscribe([=](QFuture<int> future) {
    // You may use QFuture as the input argument of your callback function
    // It will be set to the observed future object. So that you may obtain
    // the value of results()

    qDebug() << future.results();

    // Return another QFuture is possible.
    return QtConcurrent::run(reducerFunc, future.results());
}).future();

// f2 is constructed before the QtConcurrent::run statement
// But its value is equal to the result of reducerFunc

```


More examples are available at : [asyncfuture/example.cpp at master · vpicaver/asyncfuture](https://github.com/vpicaver/asyncfuture/blob/master/tests/asyncfutureunittests/example.cpp)

Installation
=============

Add the repository to your source tree, for example as a git submodule, and include it with `add_subdirectory` as shown in [Including it into your project](#including-it-into-your-project). The templates still work from `asyncfuture.h` alone; `limited()`, `Ledger`, and `ByteLedger` need the compiled library.

API
===

AsyncFuture::observe(QObject* object, PointerToMemberFunc signal)
-------------------

This function creates an Observable&lt;ARG&gt; object which contains a future to represent the result of the signal. You could obtain the future by the future() method. And observe the result by subscribe() / context() methods

The ARG type is equal to the first parameter of the signal. If the signal does not contain any argument, ARG will be void. Note that having more than one argument may result in a syntax error.

```c++
QFuture<void> f1 = observe(timer, &QTimer::timeout).future();
QFuture<bool> f2 = observe(button, &QAbstractButton::toggled).future();
```

To support signals with multiple arguments, use a AsyncFuture::deferred, for example:

```c++
auto proc = QSharedPointer<QProcess>(new QProcess());

struct Finished {
    int exitCode = -1;
    QProcess::ExitStatus status = QProcess::CrashExit;
};

auto deferred = AsyncFuture::deferred<Finished>();
QObject::connect(proc.get(), QOverload<int, QProcess::ExitStatus>::of(&QProcess::finished),
                 [&](int code, QProcess::ExitStatus status) {
    //Signal that the deferred is done
    deferred.complete({code, status});
});

proc->start(QString("ls"), QStringList());
```


See [Observable`<T>`](#observablet)

AsyncFuture::observe(object, SIGNAL(signal))
----------------------

This function creates an `Observable<QVariant>` object which contains a future to represent the result of the signal. You could obtain the future by the future() method. And observe the result by subscribe() / context() methods. The result of the future is equal to the first parameter of the signal.

```c++
QFuture<QVariant> future = observe(timer, SIGNAL(timeout())).future();
```

See [Observable`<T>`](#observablet)

AsyncFuture::observe(QFuture&lt;T&gt; future)
-------------

This function creates an Observable&lt;T&gt; object which provides an interface for observing the input future. See [Observable`<T>`](#observablet)

```c++
// Convert a signal from QObject into QFuture
QFuture<bool> future = observe(button, &QAbstractButton::toggled).future();


// Listen from the future without using QFutureWatcher<T>
observe(future).subscribe([](bool toggled) {
    // onCompleted. It is invoked when the observed future is finished successfully
    qDebug() << "onCompleted";
},[]() {
    // onCanceled
    qDebug() << "onCancel";
});
```

AsyncFuture::observe(QFuture&lt;QFuture&lt;T&gt;&gt; future)
-----

This function creates an Observable<T> object which provides an interface for observing the input future. That is designed to handle the following use-case:

```c++
QFuture<QImage> readImagesFromFolder(const QString& folder) {

    auto worker = [=]() {
        // Read files from a directory in a thread
        QStringList files = findImageFiles(folder);

        // Concurrent image reader
        return QtConcurrent::mapped(files, readImage);
    };

    auto future = QtConcurrent::run(worker); // The type of future is QFuture<QFuture<QImage>>

    auto defer = AsyncFuture::deferred<QImage>();

    // defer object track the input future. It will emit "started" and `progressValueChanged` according to the status of the future of "QtConcurrent::mapped"
    defer.complete(future);
    return defer.future();
}
```

See [Observable`<T>`](#observablet)



AsyncFuture::combine(CombinatorMode mode = FailFast)
------------

This function creates a Combinator object (inherit `Observable<void>`) for combining multiple future objects with different type into a single future.

For example:

```c++

QFuture<QImage> f1 = QtConcurrent::run(readImage, QString("image.jpg"));
QFuture<void> f2 = observe(timer, &QTimer::timeout).future();

auto combinator = combine(AllSettled) << f1 << f2;

QFuture<QImage> result = combinator.subscribe([=](){
    // Read an image but do not return before timeout
    return f1.result();
}).future();

QCOMPARE(combinator.future().progressMaximum(), 2);
```

Once all the observed futures finished, the contained future will be finished too.  And it will be cancelled immediately if any one of the observed future is cancelled in fail fast mode. In case you want the cancellation take place after all the futures finished, you should set mode to `AsyncFuture::AllSettled`.

Since v0.4.1, the `progressValue` and `progressMaximum` of the obtained future will be set.

Since v0.3.6, you may assign a deferred object to Combinator directly.

Example

```c++
QFuture<QImage> f1 = QtConcurrent::run(readImage, QString("image.jpg"));
auto defer = deferred<void>();

QFuture<QImage> result = (combine(AllSettled) << f1 << defer).subscribe([=](){
    // Read an image but do not return before the deferred is completed
    return f1.result();
}).future();
```

Because a Combinator stays open for more `<<` inputs, it cannot settle until it has been through the event loop, even when every input handed to it has already finished. If the input set is known up front, use the fixed-set form below instead.

AsyncFuture::combine(fixed set, CombinatorMode mode = FailFast)
------------

```c++
Observable<void> combine(std::initializer_list<QFuture<void>> futures, CombinatorMode mode = FailFast);

template <typename T>
Observable<void> combine(const QList<QFuture<T>>& futures, CombinatorMode mode = FailFast);
```

These overloads join a set of futures that is closed on return. Cancellation, progress and `CombinatorMode` behave exactly as they do for `combine() << f`; the difference is that the set cannot grow afterwards, so inputs that have already finished are settled inline and a fully finished set joins without an event loop turn.

```c++
auto joined = combine({leftJob, rightJob});

// If both jobs were already finished - a cache hit, or a node whose
// dependencies were all satisfied earlier - this is already true here,
// with no trip through the event loop.
joined.future().isFinished();

joined.context(this, [=]() {
    // Callbacks are still delivered asynchronously, as always.
});
```

The gain compounds in a dependency graph: a chain of `Deferred::complete()` and `combine({...})` over work that is already done settles end to end in one turn rather than one turn per level.

They return an `Observable<void>` rather than a `Combinator`, so `<<` is a compile error on the result — the set is sealed, and an input added after the join settled would be silently ignored:

```c++
combine({f1, f2}) << f3;   // error: invalid operands to binary expression
```

The empty set completes immediately, on the grounds that a join over no dependencies is a satisfied precondition. Note this differs from `combine() << QList<QFuture<void>>()`, which cancels.

AsyncFuture::deferred&lt;T&gt;()
----------

The deferred() function return a Deferred object that allows you to set QFuture completed/cancelled manually. 

```c++
auto d = deferred<bool>();

d.subscribe([]() {
    qDebug() << "onCompleted";
}, []() {
    qDebug() << "onCancel";
});

d.complete(true); // or d.cancel();
```

See [`Deferred<T>`](#deferredt)

AsyncFuture::shield(QFuture&lt;T&gt; future)
----------

The shield() function returns a future that forwards the input's completion, cancellation, and progress, but does not push a cancel of the returned future back to the input — the same semantics as Python's `asyncio.shield`. The input future stays directly cancelable by whoever holds it; shield only stops cancellation from arriving through the returned handle.

Use it when handing a shared future to multiple consumers, so one consumer's `cancel()` cannot poison the source for every other observer:

```c++
QFuture<void> Queue::drained() {
    // Consumers may cancel their copy freely; the shared m_drainDeferred
    // future underneath is untouched and other observers still complete.
    return AsyncFuture::shield(m_drainDeferred.future());
}

auto future = queue->drained();
future.cancel();   // this consumer stops observing; the queue keeps draining
```

It is a convenience wrapper over `Deferred<T>::complete(future, CancelPropagation::Blocked)`. See [`Deferred<T>`](#deferredt)

![AsyncFuture Class Diagram](https://raw.githubusercontent.com/benlau/junkcode/master/docs/AsyncFuture%20Class%20Diagram.png)

Observable&lt;T&gt;
------------

Observable&lt;T&gt; is a chainable utility class for observing a QFuture object. It is created by the observe() function. It can register multiple callbacks to be triggered in different situations. And that will create a new Observable&lt;T&gt; / QFuture object to represent the result of the callback function. It may even call QtConcurrent::run() within the callback function to run the funciton in another thread. Therefore, it could create a more complex/flexible workflow.

```c++
QFuture<int> future;

Observable<int> observable1 = AsyncFuture::observe(future);
// or
auto observable2 = AsyncFuture::observe(future);
```
**QFuture&lt;T&gt; Observable&lt;T&gt;::future()**

Obtain the QFuture object to represent the result.

**Observable&lt;T&gt; Observable&lt;T&gt;::subscribe(Completed onCompleted, Canceled onCanceled)**

    Observable<T> Observable<T>::subscribe(Completed onCompleted);
    Observable<T> Observable<T>::subscribe(Completed onCompleted, Canceled onCanceled);

Register a onCompleted and/or onCanceled callback to the observed QFuture object. Unlike the context() function, the callbacks will be triggered on the main thread. The return value is an `Observable<R>` object where R is the return type of the onCompleted callback.

Remarks: Before v0.3.2, the callback will be executed in the current thread.

See [Subscribe Callback Function](#subscribe-callback-funcion)

Example:

```c++
QFuture<bool> future = observe(button, &QAbstractButton::toggled).future();

// Listen from the future without using QFutureWatcher<T>
observe(future).subscribe([](bool toggled) {
    // onCompleted. It is invoked when the observed future is finished successfully
    qDebug() << "onCompleted";
},[]() {
    // onCanceled
    qDebug() << "onCancel";
});

```

**Observable&lt;R&gt; Observable&lt;T&gt;::context(QObject&#42; contextObject, Completed onCompleted, Cancel onCanceled)**

*This API is for advanced users only*

Add a callback function that listens to the finished and canceled signals from the observing QFuture object.

The callback is invoked in the thread of the context object. In case the context object is destroyed before the finished signal, the callback functions (onCompleted and onCanceled) won't be triggered and the returned Observable object will cancel its future.

Note: An event loop, must be excuting on the the contextObject->thread() for nested observe().context() calls to work.
Threads on the QThreadPool, generally don't have a QEventLoop executing, so manually creating and calling QEventLoop is
necessary. For example:

```c++
auto worker = [&]() {
    auto localTimeout = [](int sleepTime) {
        return QtConcurrent::run([sleepTime]() {
            QThread::currentThread()->msleep(sleepTime);
        });
    };

    QEventLoop loop;

    auto context = QSharedPointer<QObject>::create();

    QThread* workerThread = QThread::currentThread();

    observe(localTimeout(50)).context(context.data(), [localTimeout, context]() {
        qDebug() << "First time localTimeout() finished";
        return localTimeout(50);
    }).context(context.data(), [context, &called, workerThread, &loop]() {
        qDebug() << "Second time localTimeout() finished";
        loop.quit();
    });

    loop.exec();
};

QtConcurrent::run(worker);

```

The return value is an `Observable<R>` object where R is the return type of the onCompleted callback.

```c++

auto validator = [](QImage input) -> bool {
   /* A dummy function. Return true for any case. */
   return true;
};

QFuture<QImage> reading = QtConcurrent::run(readImage, QString("image.jpg"));

QFuture<bool> validating = observe(reading).context(contextObject, validator).future();
```

In the above example, the result of `validating` is supposed to be true. However, if the `contextObject` is destroyed before `reading` future finished, it will be cancelled and the result will become undefined.

**void Observable&lt;T&gt;::onProgress(Functor callback)**

Listens the `progressValueChanged` and `progressRangeChanged` signal from the observed future then trigger the callback. The callback function may return nothing or a boolean value. If the boolean value is false, it will remove the listener such that the callback will not be  triggered anymore.

Example

```c++
QFuture<int> future = QtConcurrent::mapped(input, workerFunction);

AsyncFuture::observe(future).onProgress([=]() -> bool {
    qDebug() << future.progressValue();
    return true;
});

// or

AsyncFuture::observe(future).onProgress([=]() -> void {
    qDebug() << future.progressValue();
});
```

Added since v0.3.6.4

**Chained Progress**

`observe().subscribe().future()` future will report progress accordingly to the underlying future chain. When watching the final future in the chain, `progressRangeChanged` may be updated multiple times as futures in the chain update their individual `progressRangeChanged`. When visualizing final future's progress in a progress bar, progressValue may appear to go in reverse, as progressRange increases. `progressValueChanged` will never go down as execution continues. 

Example:

```c++
    QVector<int> ints(100);
    std::iota(ints.begin(), ints.end(), ints.size()); // Make ints from 1 to 100, increment by 1
    
    // Worker function
    std::function<int (int)> func = [](int x)->int {
        QThread::msleep(100);
        return x * x;
    };
    
    //First execution of workers
    //Will increase the progressRange to 100
    QFuture<int> mappedFuture = QtConcurrent::mapped(ints, func);

    auto nextFuture = AsyncFuture::observe(mappedFuture).subscribe([ints, func](){
        //Execute another batch of workers
        //Will increase the progressRange to 200
        QFuture<int> mappedFuture2 = QtConcurrent::mapped(ints, func);
        return mappedFuture2;
    }).future();

    AsyncFuture::observe(nextFuture).onProgress([nextFuture](){
        //Report the progress for the sum of mappedFuture and nextFuture from 0 to 200.
    });
```

Deferred&lt;T&gt;
-----------

The `deferred<T>()` function return a Deferred<T> object that allows you to manipulate a QFuture manually. The future() function return a running QFuture<T>. You have to call Deferred.complete() / Deferred.cancel() to trigger the status changes.

The usage of complete/cancel in a Deferred object is pretty similar to the resolve/reject in a Promise object. You could complete a future by calling complete with a result value. If you give it another future, then it will observe the input future and change status once that is finished.

For a `deferred<T>()` that is created and immediately completed, it's recommended to use `completed<T>()` instead.

**Auto Cancellation**

The `Deferred<T>` object is an explicitly shared class. You may own multiple copies and they are pointed to the same piece of shared data. In case, all of the instances are destroyed, it will cancel its future automatically.

But there has an exception if you have even called Deferred.complete(`QFuture<T>`) / Deferred.cancel(`QFuture<ANY>`) then it won't cancel its future due to destruction. That will leave to the observed future to determine the final state.

```c++
  QFuture<void> future;
  {

    auto defer = deferred<void>();
    future = defer.future();
  }
  QCOMPARE(future.isCanceled(), true); // Due to auto cancellation
```

```c++
  QFuture<void> future;
  {

    auto defer = deferred<void>();
    future = defer.future();
    defer.complete(QtConcurrent::run(worker));
  }
  QCOMPARE(future.isCanceled(), false);
```

**Deferred&lt;T&gt;::complete(T) / Deferred&lt;T&gt;::complete()**

Complete this future object with the given arguments

**Deferred&lt;T&gt;::complete(QList&lt;T&gt;)**

Complete the future object with a list of result. User may obtain all the value by QFuture::results().

**Deferred&lt;T&gt;::complete(QFuture&lt;T&gt;, CancelPropagation cancelPropagation = CancelPropagation::Propagate)**

This future object is deferred to complete/cancel. It will track the state from the input future. If the input future is completed, then it will be completed too. That is same for cancel.

If the input future has already finished and this is called from the main thread, the deferred settles in the same event loop turn instead of waiting on a `QFutureWatcher`. Only the deferred's own state moves early — AsyncFuture observers (`observe()`, `context()`, `subscribe()`) always own a watcher, so their callbacks are still delivered asynchronously.

A Qt-native `QFuture::then()` continuation behaves the other way around: `reportFinished()` drives Qt's continuation chain on the calling thread, so it runs before `complete()` returns. A context object does not change this — `then(context, f)` uses `AutoConnection`, which is a direct call when the deferred settles on the context's own thread. This is not new: `complete(value)` has always settled synchronously and always ran such continuations inline. What changed is that `complete(finishedFuture)` now does the same, rather than deferring them by one turn.

Settling immediately also closes the deferred's write window immediately, where previously it stayed open for one event loop turn. Every "second writer" that used to win that race is now a no-op:

```c++
defer.complete(finishedFuture);
defer.complete(42);                // used to overwrite; now ignored, result stays that of finishedFuture
```

This is the same rule as `complete(value)`, which has always refused a second write. The cancel-propagation section below covers the one remaining face of it.

The most visible face of this is `Deferred<T>::cancel()`, which has always been a no-op once the future has settled — the guard exists because a retroactive cancel makes every observer downstream drop its callback and its results. Previously `complete(finishedFuture)` left a window in which the deferred had not settled yet, so an immediately following `cancel()` slipped past that guard and canceled. It only did so if the event loop had not run in between, which made it a race rather than a contract:

```c++
defer.complete(finishedFuture);
defer.cancel();                    // used to cancel; now a no-op, as after complete(value)

defer.complete(finishedFuture);
qApp->processEvents();
defer.cancel();                    // was already a no-op, before and after
```

Canceling the `QFuture` handle rather than the Deferred is unaffected and still follows Qt, where canceling a finished future does set `isCanceled()`:

```c++
defer.complete(finishedFuture);
auto future = defer.future();
future.cancel();
future.isCanceled();               // true, on Qt's own semantics
```

By default, canceling this deferred's future is also pushed upstream to the input future (`CancelPropagation::Propagate`). Pass `CancelPropagation::Blocked` to keep the forwarding one-way: this deferred's future stays cancelable, but the cancel is not forwarded to the input, so upstream work continues and other observers of a shared input future are unaffected. `AsyncFuture::shield()` is a convenience wrapper for the Blocked policy.

Propagation applies to inputs that are still running. An input that had **already finished** when it was passed to `complete()` is never marked canceled after the fact:

```c++
defer.complete(finishedFuture);    // Propagate, the default
defer.future().cancel();
finishedFuture.isCanceled();       // false; was true before the fast path
```

`Propagate` and `Blocked` are therefore indistinguishable for an already-finished input. This matters because Qt's `cancel()` does set the flag on a finished future, so the old behavior reached back and marked completed upstream work as canceled — poisoning it for every other observer of that shared future, which is the same hazard `shield()` exists to prevent. There is nothing left to stop in a future that has already finished, so nothing is propagated.


**Deferred&lt;T&gt;::cancel()**

Cancel the future object

**Deferred&lt;T&gt;::cancel(QFuture<ANY>)**

This future object is deferred to cancel according to the input future. Once it is completed, this future will be cancelled. However, if the input future is cancelled. Then this future object will just ignore it. Unless it fulfils the auto-cancellation rule.

**Deferred&lt;T&gt;::track(QFuture target)**

Track the progress and synchronize the status of the target future object.

It will forward the signal of `started`, `resumed`, `paused`. And synchronize the `progressValue`, `progressMinimum` and `progressMaximum` value by listening the `progressValueChanged` signal from target future object.

Remarks: It won't complete a future even the `progressValue` has been reached the maximum value.

Added since v0.3.6

completed()
-----------

The `completed<T>(const T&)` and `completed()` function return finished `QFuture<T>`
and `QFuture<void>` . `completed<T>(const T&)` can be used instead of a `deferred<T>()`
when the result is already available. For example:

```c++
    auto defer = deferred<int>();
    defer.complete(5);
    auto completedFuture = defer.future();
```

is equivalent to

```c++
    auto completedFuture = completed<int>(5);
```

`completed<T>(const T&)` is more convenient and light weight (memory and performance efficient) method of creating
a completed `QFuture<T>`.

Example

```c++
auto defer = AsyncFuture::deferred<void>();

auto mappedFuture = QtConcurrent::mapped(data, workerFunc);

defer.track(mappedFuture);

AsyncFuture::observe(mappedFuture).subscribe([=]() mutable {
   defer.complete();
});

return defer.future(); // It is a future with progress value same as the mappedFuture, but it don't contains the result.
```

Advanced Topics
=======

Subscribe Callback Funcion
---------------

In subscribe() / context(), you may pass a function with zero or one argument as the onCompleted callback. If you give it an argument, the type must be either of T or QFuture<T>. That would obtain the result or the observed future itself.

```c++
QFuture<QImage> reading = QtConcurrent::run(readImage, QString("image.jpg"));

observe(reading).subscribe([]() {
});

observe(reading).subscribe([](QImage result) {
});

observe(reading).subscribe([](QFuture<QImage> future) {
  // In case you need to get future.results
});
```

The return type can be none or any kind of value. That would determine what type of `Observable<R>` generated by context()/subscribe().

In case, you return a QFuture object. Then the new `Observable<R>` object will be deferred to complete/cancel until your future object is resolved. Therefore, you could run QtConcurrent::run within your callback function to make a more complex/flexible multi-threading programming models.

```c++

QFuture<int> f1 = QtConcurrent::mapped(input, mapFunc);

QFuture<int> f2 = observe(f1).context(contextObject, [=](QFuture<int> future) {
    // You may use QFuture as the input argument of your callback function
    // It will be set to the observed future object. So that you may obtain
    // the value of results()

    qDebug() << future.results();

    // Return another thread is possible.
    return QtConcurrent::run(reducerFunc, future.results());
}).future();

// f2 is constructed before the QtConcurrent::run statement
// But its value is equal to the result of reducerFunc

```

Callback Chain Cancelation
----

A chain can be canceled by returning a canceled QFuture.

Example:

```c++
auto f2 = observe(f1).subscribe([=]() {
  auto defer = Deferred<void>();
  defer.cancel();
  return defer.future();
}).future();

observe(f2).subscribe([=]() {
  // it won't be executed.
});
```

Cancelling the future at the end of the chain will cancel the whole chain. This will cancel all `QtConcurrent` execution. Worker threads that have already been started by `QtConcurrent` will continue running until finished, but no new ones will be started (this is how `QtConcurrent` works). 

Example: 

```c++
    QVector<int> ints(100);
    std::iota(ints.begin(), ints.end(), ints.size());
    std::function<int (int)> func = [](int x)->int {
        QThread::msleep(100);
        return x * x;
    };
    
    QFuture<int> mappedFuture = QtConcurrent::mapped(ints, func);

    auto future = AsyncFuture::observe(mappedFuture).subscribe(
                []{ 
                   // it won't be executed
                },
                []{ 
                    // it will be executed.
                }
    ).future();
    
    future.cancel(); //Will cancel mappedFuture and future 
```

Future Object Tracking
---------------

Since v0.4, the deferred object is supported to track the status of another future object. It will synchronize the `progressValue` / `progressMinimum` / `progressMaximum` and status of the tracking object. (e.g started signal)

For example:

```c++

        auto defer = AsyncFuture::deferred<QImage>();

        QFuture<QImage> input = QtConcurrent::mapped(files, readImage);

        defer.complete(input); // defer.future() will be a mirror of `input`. The `progressValue` will be changed and it will emit "started" signal via QFutureWatcher
```

A practical use-case

```c++

QFuture<QImage> readImagesFromFolder(const QString& folder) {

    auto worker = [=]() {
        // Read files from a directory in a thread
        QStringList files = findImageFiles(folder);

        // Concurrent image reader
        return QtConcurrent::mapped(files, readImage);
    };

    auto future = QtConcurrent::run(worker); // The type of future is QFuture<QFuture<QImage>>

    auto defer = AsyncFuture::deferred<QImage>();

    // defer object track the input future. It will emit "started" and `progressValueChanged` according to the status of the future of "QtConcurrent::mapped"
    defer.complete(future);
    return defer.future();
}

```

In the example code above, the future returned by `defer.future` is supposed to be a mirror of the result of `QtConcurrent::mapped`. However, the linkage is  not estimated in the beginning until the worker functions start `QtConcurrent::mapped`

In case it needs to track the status of a future object but it won’t complete automatically. It may use track() function

Restarter&lt;T&gt;
---
Restarter is a helper for managing asynchronous operations in Qt/Concurrent. It lets you “restart” a long-running task whenever inputs change, ensuring that only the most recent invocation actually runs to completion. Previous runs are cancelled cleanly before the next one begins.

**When to use**

- **Debouncing or reload-on-change** — If you have a UI element (e.g. a search box, filter slider, map viewport) that triggers an expensive asynchronous operation on every change, use Restarter to avoid piling up simultaneous tasks. Only the latest action will complete; in-flight tasks get cancelled.
- **Auto-retry with updated parameters** — When your parameters change mid-flight, you want to cancel the current job and start a fresh one with new arguments, but only once the old job actually acknowledges cancellation.

**How it works**

1. First call to restart(...)
  * Immediately runs the provided runFunction to produce a QFuture<T>.
  * Calls your “future changed” callback so you can react to the new future.
2. Subsequent calls while the future is running
  * Saves the newest runFunction.
  * Installs a single cancellation watcher (via AsyncFuture::observe) on the running future.
  * Cancels the running future.
3. When the running future is cancelled
  * The watcher fires, and Restarter calls your latest runFunction to start the new task.
  * Again notifies you via the “future changed” callback.
4. Safety on context deletion
  * Because the watcher is tied to a QObject* context, if that context is destroyed before cancellation completes, the watcher is cleaned up and you’ll get a cancelled future.

**Cancelling from the outside**

Calling `restarter.future().cancel()` cancels both the outer future the user is holding **and** the inner running `QFuture` — so a worker that polls `promise.isCanceled()` will see the cancel and can short-circuit. After an external cancel, the next call to `restart()` produces a fresh outer future (a new `restarter.future()`); previously held handles to the cancelled future stay cancelled.

**Quick Example**

Imagine you have a search field and want to fire off a network request each time the text changes, but cancel any pending request as soon as the user types again:

```c++
// 1) Create a Restarter<int> (or whatever T your request returns)
Restarter<ResponseData> restarter{ this };

// 2) Hook up a callback so you get notified whenever a new future is created
restarter.onFutureChanged([&](){
  //Help watch current progress of the future that's running
});

// 3) Whenever the text changes, restart the search
connect(searchLineEdit, &QLineEdit::textChanged, this,
        [&](const QString& text){
    restarter.restart([=](){
        // This lambda runs on a worker thread
        auto future = QtConcurrent::run([=](){
            return performNetworkSearch(text);
        });

        return observe(future).subscribe([](){
          //Handle the results of network search
        }).future();

    });
});
```
* On the very first keystroke, a network request fires immediately.
* If the user types again before the request completes, the old request is cancelled (assuming your network layer supports cancellation) and a new one starts only once the cancellation is acknowledged.
* displayResults() sees only the latest completed response.

**Consuming the result: `onResult(context, callback)`**

`onFutureChanged` fires when `future()` starts pointing at a *new* future — it does **not** hand you the settled value. The historical way to consume a result was to re-attach an observer inside the changed callback and re-derive a cancel/empty guard by hand:

```c++
restarter.onFutureChanged([&]() {
    AsyncFuture::observe(restarter.future()).context(this, [&]() {
        auto f = restarter.future();
        if (f.isCanceled() || f.resultCount() == 0) return; // easy to forget / get wrong
        consume(f.result());
    });
});
```

That two-step is exactly where a double-delivery bug can hide: re-attaching `observe(future()).context()` on every change, if the changed callback ever fires on mere *completion*, attaches a second observer to an already-finished future and delivers twice. `onResult` encapsulates the whole pattern — the `observe(future()).context()` wiring **and** the guard — so you only write the consumption body:

```c++
// Fires callback exactly once per logical run, on `context`'s thread, only for
// a completed (non-cancelled, non-empty) outer future. Never fires after
// `context` is destroyed.
restarter.onResult(this, [this](const ResponseData& result) {
    displayResults(result);   // cancel/empty already filtered out
});
```

* **Non-void `T`** — the callback receives the result. Take it by `const&` if you only read it, or by value (`ResponseData result`) if you want a movable local you can `std::move` from — the delivered result is a prvalue, so by-value adds no copy over `const&`.
* **`Restarter<void>`** — the callback takes no argument: `restarter.onResult(this, [this]() { ... });`.
* **Guard** — a cancelled run (superseded by a newer `restart()`, or an explicit `future().cancel()`) and a finished-but-empty run (`resultCount() == 0`, e.g. a worker that returned without `addResult`) are both dropped before your callback runs.
* **Lifetime** — delivery is bound to `context`. `onResult` captures each run's future by value, so a later `restart()` never redirects an already-attached observer, and the callback never touches a destroyed `Restarter`.

`onResult` **composes with** `onFutureChanged`: keep a thin `onFutureChanged` for side work that must happen when the run *starts* (e.g. registering the job with a future manager) and let `onResult` handle delivery. Both fire when a fresh future is installed:

```c++
restarter.onFutureChanged([this]() {
    m_futureManagerToken.addJob({ QFuture<void>(restarter.future()), "Loading…" });
});
restarter.onResult(this, [this](const ResponseData& result) {
    applyResult(result);
});
```


limited(context, inputs, launch, [weight,] limit)
---
`limited()` runs one launcher per input through a window instead of starting them all at once. An input is admitted, in input order, while fewer than `limit.maxRunning` items run and, when `limit.budget` is positive, the running items' summed `weight` plus its own fits the budget. As each item's launched future finishes, its slot and weight are returned and the next inputs are admitted.

**When to use**

- **Bounded fan-out** — a pipeline with hundreds of items where each one decodes something large. Start as many as the memory budget holds, and start the next as each finishes.
- **Cheap queued inputs** — `launch` is called only on admission, so a queued input stays a small value and the heavy data is built inside the launcher.

```c++
using namespace AsyncFuture;

// Owned by the application and shared by every run; it must outlive them.
ByteLedger* ledger = application->memoryLedger();

auto run = limited(this, inputs,
    [](const Input& input) { return QtConcurrent::run(process, input); },  // Input -> QFuture<Result>
    [](const Input& input) { return estimatedBytes(input); },              // Input -> qint64
    Limit{QThread::idealThreadCount(), kBudgetBytes, ledger});

for (const QFuture<Result>& item : run.items) {
    observe(item).context(this, [this](const Result& result) { deliver(result); });
}
// run.all.cancel() stops admission and cancels every item.
```

* **`items`** — one future per input, in input order. Each exists before its input is admitted, so it can be observed right away; admission completes it with the launched future through `Deferred<T>::complete(future, CancelPropagation::Propagate)`, so canceling an admitted item cancels the launched work.
* **`all`** — completes once every item is skipped or its launched future has finished. Canceling it cancels the running items and every queued one, and admits nothing more.
* **First item always runs** — when nothing is running, the next item is admitted whatever its weight, so an item heavier than the whole budget still runs, alone.
* **Skipping** — canceling one item's future before its turn skips it.
* **Cancel** — canceling an item, `all`, or destroying `context` cancels the item's future at once. Its running slot, weight, and ledger charge return only when the launched future reports finished. A canceled `QPromise` or `QtConcurrent` future finishes when its worker exits, so a canceled item keeps its share, and still blocks admission, until the worker is gone. When only an item is canceled, `all` completes after that item's launched future finishes. Canceling `all` or destroying `context` cancels `all` at once, and the still-running workers return their slots and ledger charges as they exit, so `all.isFinished()` there means the run stopped, while workers may still hold memory.
* **Lifetime** — destroying `context` cancels every item and `all`.
* **`Limit::budget` zero or negative** — counts items only; `weight` is still called but never blocks admission.
* **Deduced `T`** — the result type comes from the launcher's `QFuture<T>`, so no template argument is needed. A launcher that returns anything other than a `QFuture` fails to compile with a `static_assert`.
* **Count-only overload** — `limited(this, inputs, launch, Limit{2})` drops `weight`. Every item then weighs one, so `Limit::budget` and `Limit::ledger` count items. `Limit` is always the last argument, so a weight lambda with no `Limit` fails to compile.
* **Weight** — `weight` is called at most once per input, and only for an input the count limit would admit.

`limited()` is not thread-safe: call it from `context`'s thread, the same contract `Restarter` has. `launch` and `weight` run on that thread, and re-admission runs there through a `QFutureWatcher` on each launched future, connected to `finished` only and owned by `context`.

**Sharing a cap across runs: `Ledger`**

`Limit::budget` caps one run. When several runs, possibly on different threads, must share one cap, such as a process-wide memory budget, pass a `Ledger` in `Limit::ledger`. Each admitted item charges its weight to the ledger, and the ledger gets it back when the item's launched future finishes.

```c++
class Ledger : public QObject {
    Q_OBJECT
public:
    virtual bool tryCharge(qint64 bytes) = 0; // charges and returns true when the bytes fit
    virtual void charge(qint64 bytes) = 0;    // charges whether or not the bytes fit
    virtual void release(qint64 bytes) = 0;   // returns bytes, then emits released()
signals:
    void released();
};
```

`ByteLedger(cap)` is the shipped implementation: a fixed cap guarded by a mutex, with `cap()` and `charged()` for diagnostics. `tryCharge` refuses without recording anything when the bytes would pass the cap. Zero and negative byte counts are no-ops. Subclass `Ledger` for any other policy; implementations must be thread-safe.

* **First item** — the first item of a window always runs and is charged in full through `charge()`, even past the cap. Every other run sharing the ledger then sees the ledger full and holds back while it runs.
* **Other items** — charged through `tryCharge()`; a refusal stops admission in that run until bytes come back.
* **Wakeup** — `release()` emits `released()`, and every run sharing the ledger retries admission on its own context's thread. A release in one run therefore admits waiting items in the others.
* **Lifetime** — the application owns the ledger, and it must outlive every run that uses it. `Limit::ledger` is a non-owning pointer and defaults to `nullptr`, meaning no ledger. When a context is destroyed while items run, their bytes return to the ledger as each launched future finishes, on the main thread.


Cooperative cancellation inside a worker (QPromise)
---

AsyncFuture can propagate cancellation up a chain — `chain.cancel()` flows back to the source `QFuture` — but **QtConcurrent workers don't abort mid-function automatically.** A worker only stops if it cooperates by polling the cancellation state. The Qt-native way to do that is to take a `QPromise<T>&` argument:

```cpp
auto future = QtConcurrent::run([input](QPromise<QImage>& promise) {
    for (int i = 0; i < bigN; i++) {
        if (promise.isCanceled()) return;          // cooperative early-exit
        doExpensiveStep(i, input);
    }
    promise.addResult(renderResult(input));
});
```

`promise.isCanceled()` and the returned `QFuture<T>` share cancel state, so **any cancel that back-propagates to that future — a Restarter restart, a direct `future.cancel()`, or a chain-end cancel via `observe().subscribe().future().cancel()` / `observe().context(ctx, ...).future().cancel()` — flips the flag the worker is polling.**

> **Caveat on `context()` destruction:** destroying the context object only cancels the Observable's returned future — it does **not** back-propagate to the upstream worker. If you want context-scoped lifetime *and* cooperative worker cancellation, call `chain.cancel()` explicitly when the scope ends, or use `Restarter` (which bakes the cancel-in-flight behaviour in).

Works identically with `Restarter`, `subscribe`, and `context`:

**With Restarter** — rapid restarts cancel the in-flight worker:

```cpp
Restarter<QImage> restarter{ this };
restarter.restart([input]() -> QFuture<QImage> {
    return QtConcurrent::run([input](QPromise<QImage>& promise) {
        for (int i = 0; i < bigN; i++) {
            if (promise.isCanceled()) return;
            doExpensiveStep(i, input);
        }
        promise.addResult(finalize(input));
    });
});
// A second restart(...) call cancels the running worker via its QPromise.
```

**With `observe().subscribe()`** — cancel the chain end and the worker sees it:

```cpp
auto work = QtConcurrent::run([](QPromise<int>& p) {
    for (int i = 0; i < bigN; i++) {
        if (p.isCanceled()) return;
        doStep(i);
    }
    p.addResult(42);
});

auto chain = observe(work).subscribe(
    [](int r) { /* onCompleted */ },
    []()      { /* onCancelled */ }
).future();

chain.cancel();   // propagates up to `work`, which flips promise.isCanceled()
```

**With `observe().context()`** — explicit chain-end cancel back-propagates into the worker:

```cpp
auto work = QtConcurrent::run([](QPromise<int>& p) { /* polling loop */ });
auto chain = observe(work).context(contextObject,
    [](int r) { /* onCompleted */ },
    []()      { /* onCancelled */ }
).future();

chain.cancel();   // propagates up to `work`, which flips promise.isCanceled()
```

If you need worker cancellation tied to the lifetime of `contextObject`, call `chain.cancel()` from the context's destructor (or simpler: reach for `Restarter`).

**Rules of thumb:**

- Poll `promise.isCanceled()` between expensive steps — it's an atomic load, cheap per iteration. For *very* tight inner loops, check every N iterations.
- On the cancelled path, **do not call `promise.addResult()`**. The downstream observer reads `future.resultCount() == 0` (or `future.isCanceled()`) to skip any "on success" side-effects.
- `promise.setProgressValue()` and `promise.setProgressRange()` are also available on the same promise if you want a progress bar for the same worker.
- This pattern is not specific to `Restarter` — any `QFuture<T>` produced by `QtConcurrent::run([](QPromise<T>&){...})` supports it, regardless of what chain sits on top.

See `Spec::test_restarter_qpromise_cancel_inner_loop`, `Spec::test_subscribe_qpromise_cancel_inner_loop`, and `Spec::test_context_qpromise_cancel_inner_loop` for executable examples of each.

Reporting progress from a QPromise worker
---

The same `QPromise<T>&` parameter that drives cooperative cancellation also drives progress: call `promise.setProgressRange(min, max)` once, then `promise.setProgressValue(i)` as work advances. The returned `QFuture<T>` emits `progressRangeChanged` / `progressValueChanged` exactly as if you had used `QtConcurrent::mapped` — AsyncFuture doesn't need to know the worker is QPromise-based.

```c++
auto future = QtConcurrent::run([input](QPromise<int>& promise) {
    promise.setProgressRange(0, input.size());
    for (int i = 0; i < input.size(); ++i) {
        if (promise.isCanceled()) return;
        doStep(input[i]);
        promise.setProgressValue(i + 1);
    }
    promise.addResult(input.size());
});
```

**Through `.context()` / `.subscribe()`** — the chain's returned future mirrors the source's progress. Attach a `QFutureWatcher<T>` to the chain, not to the source:

```c++
auto chain = observe(work).context(ctx, [](int r) { return r; }).future();

QFutureWatcher<int> watcher;
connect(&watcher, &QFutureWatcher<int>::progressValueChanged,
        [&](int v) { /* update UI */ });
watcher.setFuture(chain);
```

**Through `Restarter`** — the outer future (`restarter.future()`) forwards progress from whichever inner worker is currently in-flight, and swaps sources cleanly on each `restart(...)`. Useful for a single progress bar bound to "whatever task is running right now":

```c++
Restarter<int> restarter{ this };
restarter.restart([input]() -> QFuture<int> {
    return QtConcurrent::run([input](QPromise<int>& promise) {
        promise.setProgressRange(0, input.size());
        for (int i = 0; i < input.size(); ++i) {
            if (promise.isCanceled()) return;
            doStep(input[i]);
            promise.setProgressValue(i + 1);
        }
        promise.addResult(input.size());
    });
});

QFutureWatcher<int> watcher;
connect(&watcher, &QFutureWatcher<int>::progressValueChanged,
        [&](int v) { /* update UI */ });
watcher.setFuture(restarter.future());
```

**Chained workers accumulate** — when a `context()` / `subscribe()` callback returns another `QFuture<T>`, the chain's `progressMaximum` becomes the *sum* of the source's range and the inner's range (see the "Chained Progress" section above). A two-stage pipeline with stages of 10 and 20 steps reports a total range of 30:

```c++
auto outer = runStage(10);          // QPromise worker, range 0..10
auto chain = observe(outer).context(ctx, [](int) {
    return runStage(20);            // QPromise worker, range 0..20
}).future();
// chain.progressMaximum() == 30 once both stages finish.
```

> **Gotcha for tests:** `observe(f).context(...)` seeds the chain's range **synchronously** from `f.progressMinimum()` / `f.progressMaximum()` before `.future()` returns. A `QFutureWatcher` attached *after* that point will not see the initial `progressRangeChanged` — check `chain.progressMaximum()` directly instead. Progress *values* are always delivered through queued signals as the worker runs, so watchers capture those normally. `Restarter` queues its first `startRun()` onto the event loop, so a watcher attached to `restarter.future()` before `waitUntil(...)` sees every progress change.

**Rules of thumb:**

- Call `setProgressRange()` once at the top of the worker. Calling it again mid-run emits a new `progressRangeChanged` on the source future — which in a chain triggers a `DeferredFuture::updateProgressRanges` recomputation.
- It's safe (and idiomatic) to combine progress and cancellation on the same promise: poll `promise.isCanceled()` between steps and bail without calling `addResult()`.
- `QFutureWatcher` coalesces rapid `progressValueChanged` emissions — don't expect a 1:1 signal for every `setProgressValue` call on a fast worker.

See `Spec::test_context_qpromise_progress`, `Spec::test_restarter_qpromise_progress`, `Spec::test_restarter_qpromise_progress_across_restarts`, and `Spec::test_context_chain_qpromise_progress_accumulates` for executable examples of each pattern, plus the `ProgressCapture<T>` test helper in `testfunctions.h` for capturing progress history in assertions.

waitForFinished()
---
Sometimes you need to block until a QFuture<T> completes, but you don’t want to spin your test harness or lose signal/slot delivery in unit tests. This helper uses a QFutureWatcher<T> and a minimal QEventLoop (with an optional timeout) to pump events needed for Qt’s testing framework while waiting.

```
void MyTest::testAsyncSum() {
    auto future = QtConcurrent::run([](){
        QThread::sleep(1);
        return 123;
    });

    // Wait up to 2 seconds, processing events so QTest continues handling events
    QVERIFY(waitForFinished(future, 2000));
    QCOMPARE(future.result(), 123);
}
```



Examples
========

There has few examples of different use-cases in this source file:

[asyncfuture/example.cpp at master · vpicaver/asyncfuture](https://github.com/vpicaver/asyncfuture/blob/master/tests/asyncfutureunittests/example.cpp)

Building Testcases
==================

Open the CMakeList.txt in QtCreator and set ENABLE_TESTING to ON. Build and run ./asyncfutureunittests 

From the command line
```
cmake -S . -B build -G Ninja -DENABLE_TESTING:BOOL=ON -DCMAKE_BUILD_TYPE=Debug -DCMAKE_CXX_FLAGS="-fsanitize=address"
cmake --build build
cd build 
./tests/asyncfutureunittests
```

See [github actions](https://github.com/vpicaver/asyncfuture/tree/dev/.github/workflows) for example of building the library. 



