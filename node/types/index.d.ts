/**
 * Type declarations for `@endstone/server`.
 *
 * The module itself is served from memory by the Endstone Node host, so there is nothing to install at
 * runtime. This file exists so editors and `tsc` can see the API; the host copies it into
 * `plugins/node_modules/@endstone/server/` on startup, which is where TypeScript looks when resolving
 * from a plugin folder.
 *
 * The shape mirrors Endstone's own API (Server, Player, the PlayerXxxEvent family) rather than
 * Bedrock's ScriptAPI, so it reads 1:1 with the Endstone documentation.
 */

declare module "@endstone/server" {
    /** Log levels, matching endstone::Logger::Level. */
    export const LogLevel: {
        readonly trace: 0;
        readonly debug: 1;
        readonly info: 2;
        readonly warning: 3;
        readonly error: 4;
        readonly critical: 5;
    };

    /** Handler ordering, matching endstone::EventPriority. */
    export const EventPriority: {
        readonly lowest: 0;
        readonly low: 1;
        readonly normal: 2;
        readonly high: 3;
        readonly highest: 4;
        readonly monitor: 5;
    };

    export interface Logger {
        trace(...args: unknown[]): void;
        debug(...args: unknown[]): void;
        info(...args: unknown[]): void;
        /** Alias of `warning`. */
        warn(...args: unknown[]): void;
        warning(...args: unknown[]): void;
        error(...args: unknown[]): void;
        critical(...args: unknown[]): void;
    }

    /**
     * Anything with a presence in the world.
     *
     * Backed by a handle that is only valid inside the callback that produced it. Do not store one
     * across ticks - copy out what you need (`name`, `uniqueId`, coordinates) instead. Touching a
     * stale object throws rather than crashing the server.
     */
    export interface Actor {
        /** Endstone type identifier, e.g. `"minecraft:zombie"`. */
        readonly type: string;
        /** Name of the dimension the object is in. */
        readonly dimension: string;
        readonly x: number;
        readonly y: number;
        readonly z: number;
        readonly pitch: number;
        readonly yaw: number;
        readonly isOnGround: boolean;
        readonly isInWater: boolean;
        readonly isInLava: boolean;
        readonly isDead: boolean;
        readonly isValid: boolean;
        readonly level: Level;

        nameTag: string;
        scoreTag: string;
        isNameTagVisible: boolean;
        isNameTagAlwaysVisible: boolean;

        /** Living actors only; throws on those without health. */
        health: number;
        maxHealth: number;

        sendMessage(message: string): void;
        teleport(x: number, y: number, z: number): void;
        remove(): void;
    }

    /** A player on the server. Everything on {@link Actor} applies here too. */
    export interface Player extends Actor {
        readonly name: string;
        /** The player's UUID, as a string. */
        readonly uniqueId: string;
        readonly xuid: string;
        readonly locale: string;
        readonly deviceOs: string;
        readonly deviceId: string;
        readonly gameVersion: string;
        /** Remote hostname or IP. */
        readonly address: string;
        /** Average round-trip in milliseconds. */
        readonly ping: number;
        readonly totalExp: number;

        isOp: boolean;
        isSneaking: boolean;
        isSprinting: boolean;
        isFlying: boolean;
        isGliding: boolean;
        allowFlight: boolean;
        expLevel: number;
        expProgress: number;
        flySpeed: number;
        walkSpeed: number;

        sendErrorMessage(message: string): void;
        sendPopup(message: string): void;
        sendTip(message: string): void;
        sendTitle(title: string, fadeIn?: number, stay?: number, fadeOut?: number): void;
        resetTitle(): void;
        kick(reason: string): void;
        performCommand(command: string): void;
        updateCommands(): void;
        transfer(host: string, port?: number): void;
        giveExp(amount: number): void;
        giveExpLevels(amount: number): void;
        /** Plays at the player's own position. */
        playSound(sound: string, volume?: number, pitch?: number): void;
        stopSound(sound: string): void;
        stopAllSounds(): void;
    }

    export interface Block {
        /** Block type identifier, e.g. `"minecraft:stone"`. Assignable. */
        type: string;
        readonly dimension: string;
        readonly x: number;
        readonly y: number;
        readonly z: number;
        /** The block at the given offset from this one. */
        getRelative(dx: number, dy: number, dz: number): Block;
    }

    export interface Level {
        readonly name: string;
        /** Number of actors currently loaded. */
        readonly actorCount: number;
        readonly dimensionCount: number;
        /** World time, in ticks. Assignable. */
        time: number;
    }

    /** Common to every Endstone event. */
    export interface Event {
        /** The Endstone class name, e.g. `"PlayerJoinEvent"`. */
        readonly eventName: string;
        readonly isAsynchronous: boolean;
        readonly isCancellable: boolean;
    }

    /** An event that a plugin can stop from taking effect. */
    export interface CancellableEvent extends Event {
        /** Set to true to prevent the action. The event still reaches other plugins. */
        cancelled: boolean;
        cancel(): void;
    }

    export interface PlayerEvent extends Event {
        readonly player: Player;
    }

    export interface BlockEvent extends Event {
        readonly block: Block;
    }

    export interface PlayerJoinEvent extends PlayerEvent {
        /** Broadcast when the player joins. Set to `""` to suppress it. */
        joinMessage: string;
    }

    export interface PlayerQuitEvent extends PlayerEvent {
        quitMessage: string;
    }

    export interface PlayerChatEvent extends PlayerEvent, CancellableEvent {
        message: string;
    }

    export interface PlayerCommandEvent extends PlayerEvent, CancellableEvent {
        command: string;
    }

    export interface Subscription {
        unsubscribe(): void;
    }

    export interface SubscribeOptions {
        /** Defaults to `"normal"`. Lower priorities run first. */
        priority?: keyof typeof EventPriority;
        /** Skip delivery when an earlier handler already cancelled the event. Defaults to false. */
        ignoreCancelled?: boolean;
    }

    type Handler<E> = (event: E) => void;

    /**
     * Event subscription. Names are the Endstone event class minus `Event`, camelCased, so
     * `PlayerJoinEvent` is `events.playerJoin`.
     *
     * Handlers run synchronously on the server thread, which is what lets a handler cancel an event.
     * Keep them fast: time spent here is time the server is not ticking.
     */
    export const events: {
        playerJoin(handler: Handler<PlayerJoinEvent>, options?: SubscribeOptions): Subscription;
        playerQuit(handler: Handler<PlayerQuitEvent>, options?: SubscribeOptions): Subscription;
        playerChat(handler: Handler<PlayerChatEvent>, options?: SubscribeOptions): Subscription;
        playerCommand(handler: Handler<PlayerCommandEvent>, options?: SubscribeOptions): Subscription;
        playerLogin(handler: Handler<PlayerEvent & CancellableEvent>, options?: SubscribeOptions): Subscription;
        playerKick(handler: Handler<PlayerEvent & CancellableEvent>, options?: SubscribeOptions): Subscription;
        playerDeath(handler: Handler<PlayerEvent>, options?: SubscribeOptions): Subscription;
        playerInteract(handler: Handler<PlayerEvent & CancellableEvent>, options?: SubscribeOptions): Subscription;
        playerMove(handler: Handler<PlayerEvent & CancellableEvent>, options?: SubscribeOptions): Subscription;
        playerJump(handler: Handler<PlayerEvent>, options?: SubscribeOptions): Subscription;
        playerTeleport(handler: Handler<PlayerEvent & CancellableEvent>, options?: SubscribeOptions): Subscription;
        playerRespawn(handler: Handler<PlayerEvent>, options?: SubscribeOptions): Subscription;
        playerGameModeChange(handler: Handler<PlayerEvent & CancellableEvent>, options?: SubscribeOptions): Subscription;
        playerItemConsume(handler: Handler<PlayerEvent & CancellableEvent>, options?: SubscribeOptions): Subscription;
        playerPickupItem(handler: Handler<PlayerEvent & CancellableEvent>, options?: SubscribeOptions): Subscription;
        playerDropItem(handler: Handler<PlayerEvent & CancellableEvent>, options?: SubscribeOptions): Subscription;
        blockBreak(handler: Handler<BlockEvent & CancellableEvent>, options?: SubscribeOptions): Subscription;
        blockPlace(handler: Handler<BlockEvent & CancellableEvent>, options?: SubscribeOptions): Subscription;
        serverLoad(handler: Handler<Event>, options?: SubscribeOptions): Subscription;
        serverListPing(handler: Handler<CancellableEvent>, options?: SubscribeOptions): Subscription;
        thunderChange(handler: Handler<CancellableEvent>, options?: SubscribeOptions): Subscription;
        weatherChange(handler: Handler<CancellableEvent>, options?: SubscribeOptions): Subscription;
    };

    export interface Server {
        /** Implementation name, e.g. `"Endstone"`. */
        readonly name: string;
        readonly version: string;
        readonly minecraftVersion: string;
        /** Network protocol version, or -1 if unavailable. */
        readonly protocolVersion: number;
        /** Players currently online, or -1 before the level has loaded. */
        readonly onlinePlayerCount: number;
        /** False when the host was started without an Endstone API table. */
        readonly isAvailable: boolean;
        /**
         * The loaded level, or null before one exists.
         *
         * Unlike objects handed to an event handler, this one is safe to keep: the level lives as long
         * as the server does.
         */
        readonly level: Level | null;
        readonly logger: Logger;
        /** Broadcasts to every player and the console. */
        broadcastMessage(message: string): void;
    }

    export const server: Server;
    export const logger: Logger;

    /** A plugin's lifecycle hooks. Any may be omitted, and any may be async. */
    export interface Plugin {
        onLoad?(): void | Promise<void>;
        onEnable?(): void | Promise<void>;
        onDisable?(): void | Promise<void>;
    }

    const api: {
        server: Server;
        events: typeof events;
        logger: Logger;
        LogLevel: typeof LogLevel;
        EventPriority: typeof EventPriority;
    };
    export default api;
}
