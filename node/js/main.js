// Milestone 0 smoke test. No Minecraft API is bound yet - this only proves that Node and V8 come up
// inside the BDS process and that console output reaches the Endstone logger.
//
// The last line printed is SPIKE-COMPLETE; test drivers wait for it and then shut the server down,
// so keep it last.

console.log("Hello from Endstone Node");
console.log(`node=${process.versions.node} v8=${process.versions.v8} uv=${process.versions.uv}`);
console.log(`pid=${process.pid} platform=${process.platform} arch=${process.arch}`);
console.debug("debug level works");
console.warn("warn level works");
console.error("error level works");

// Proves microtasks drain.
Promise.resolve().then(() => console.log("microtask drained"));

// Proves the event loop is being pumped from the Endstone tick rather than run to completion.
// 100ms so the whole proof lands within a handful of ticks.
let ticks = 0;
const timer = setInterval(() => {
    ticks += 1;
    console.log(`event loop pumped, interval fired ${ticks}x`);
    if (ticks >= 3) {
        clearInterval(timer);
        console.log("interval cleared, loop still alive");
        console.log("SPIKE-COMPLETE");
    }
}, 100);
