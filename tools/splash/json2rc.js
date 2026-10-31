"use strict";
var J2RC = (() => {
  var __defProp = Object.defineProperty;
  var __getOwnPropDesc = Object.getOwnPropertyDescriptor;
  var __getOwnPropNames = Object.getOwnPropertyNames;
  var __hasOwnProp = Object.prototype.hasOwnProperty;
  var __export = (target, all) => {
    for (var name in all)
      __defProp(target, name, { get: all[name], enumerable: true });
  };
  var __copyProps = (to, from, except, desc) => {
    if (from && typeof from === "object" || typeof from === "function") {
      for (let key of __getOwnPropNames(from))
        if (!__hasOwnProp.call(to, key) && key !== except)
          __defProp(to, key, { get: () => from[key], enumerable: !(desc = __getOwnPropDesc(from, key)) || desc.enumerable });
    }
    return to;
  };
  var __toCommonJS = (mod) => __copyProps(__defProp({}, "__esModule", { value: true }), mod);

  // src/json2rc/Parser.ts
  var Parser_exports = {};
  __export(Parser_exports, {
    DeferredVariables: () => DeferredVariables,
    NotImplementedComponent: () => NotImplementedComponent,
    Parser: () => Parser,
    convert: () => convert,
    decodeBase64: () => decodeBase64,
    hAlignOf: () => hAlignOf,
    normalizeComponent: () => normalizeComponent,
    parseColor: () => parseColor,
    parseHAlign: () => parseHAlign,
    parseSvgPath: () => parseSvgPath,
    parseTextAlign: () => parseTextAlign,
    parseVAlign: () => parseVAlign,
    unwrap: () => unwrap,
    vAlignOf: () => vAlignOf
  });

  // src/json2rc/WireBuffer.ts
  var WireBuffer = class {
    constructor() {
      this.buf = new Uint8Array(1024);
      this.len = 0;
    }
    ensure(n) {
      if (this.len + n <= this.buf.length) return;
      let cap = this.buf.length * 2;
      while (cap < this.len + n) cap *= 2;
      const next = new Uint8Array(cap);
      next.set(this.buf.subarray(0, this.len));
      this.buf = next;
    }
    writeByte(value) {
      this.ensure(1);
      this.buf[this.len++] = value & 255;
    }
    writeBoolean(value) {
      this.writeByte(value ? 1 : 0);
    }
    writeShort(value) {
      this.ensure(2);
      this.buf[this.len++] = value >>> 8 & 255;
      this.buf[this.len++] = value & 255;
    }
    writeInt(value) {
      this.ensure(4);
      const v = value >>> 0;
      this.buf[this.len++] = v >>> 24 & 255;
      this.buf[this.len++] = v >>> 16 & 255;
      this.buf[this.len++] = v >>> 8 & 255;
      this.buf[this.len++] = v & 255;
    }
    writeLong(value) {
      let v = BigInt(value) & 0xffffffffffffffffn;
      this.ensure(8);
      for (let i = 7; i >= 0; i--) {
        this.buf[this.len + i] = Number(v & 0xffn);
        v >>= 8n;
      }
      this.len += 8;
    }
    writeFloat(value) {
      const dv = new DataView(new ArrayBuffer(4));
      dv.setFloat32(0, value, false);
      this.writeInt(dv.getUint32(0, false));
    }
    /** A raw 32-bit pattern — a NaN-boxed id must not be round-tripped through a float. */
    writeIntBitsAsFloat(bits) {
      this.writeInt(bits);
    }
    writeDouble(value) {
      const dv = new DataView(new ArrayBuffer(8));
      dv.setFloat64(0, value, false);
      this.writeLong(dv.getBigUint64(0, false));
    }
    write(bytes) {
      this.ensure(bytes.length);
      this.buf.set(bytes, this.len);
      this.len += bytes.length;
    }
    writeBuffer(bytes) {
      this.writeInt(bytes.length);
      this.write(bytes);
    }
    writeUtf8(s) {
      this.writeBuffer(new TextEncoder().encode(s));
    }
    /** WireBuffer.start(type): the opcode byte, nothing else — there is no length field. */
    start(opCode) {
      this.writeByte(opCode);
    }
    overwriteInt(position, value) {
      const v = value >>> 0;
      this.buf[position] = v >>> 24 & 255;
      this.buf[position + 1] = v >>> 16 & 255;
      this.buf[position + 2] = v >>> 8 & 255;
      this.buf[position + 3] = v & 255;
    }
    /**
     * Move the tail `[beyond, size)` so it starts at `insertLocation`.
     *
     * How a `global` section is hoisted ahead of the root: the ops are written in place and
     * then relocated. The guards are silent no-ops in the reference, so they are here too.
     */
    moveBlock(beyond, insertLocation) {
      if (insertLocation < 0 || beyond > this.len || insertLocation >= beyond) return;
      const tail = this.buf.slice(beyond, this.len);
      const head = this.buf.slice(insertLocation, beyond);
      this.buf.set(tail, insertLocation);
      this.buf.set(head, insertLocation + tail.length);
    }
    get index() {
      return this.len;
    }
    toBytes() {
      return this.buf.slice(0, this.len);
    }
  };
  function floatToRawIntBits(f) {
    const dv = new DataView(new ArrayBuffer(4));
    dv.setFloat32(0, f, false);
    return dv.getUint32(0, false);
  }
  function asNanBits(id) {
    return (id | 4286578688) >>> 0;
  }
  function idFromNanBits(bits) {
    return bits & 8388607;
  }
  function f32FromBits(bits) {
    const b = new ArrayBuffer(4);
    new DataView(b).setUint32(0, bits >>> 0);
    return new DataView(b).getFloat32(0);
  }

  // src/json2rc/Header.ts
  var HEADER_OP = 0;
  var MAGIC_NUMBER = 76283904;
  var MAJOR_VERSION = 1;
  var MINOR_VERSION = 1;
  var PATCH_VERSION = 0;
  var DATA_TYPE_INT = 0;
  var DATA_TYPE_FLOAT = 1;
  var DATA_TYPE_STRING = 3;
  var HEADER_KEY_TO_TAG = {
    width: 5,
    height: 6,
    desiredFPS: 8,
    fps: 8,
    contentDescription: 9,
    profiles: 14,
    featurePaintMeasure: 15,
    debug: 16,
    theme: 21,
    ltResize: 24,
    densityBehavior: 27
  };
  function writeMap(buffer, tags) {
    for (const [tag, value] of tags) {
      if (typeof value === "string") {
        buffer.writeShort((tag | DATA_TYPE_STRING << 10) & 65535);
        const data = new TextEncoder().encode(value);
        buffer.writeShort(data.length + 4);
        buffer.writeBuffer(data);
      } else if (Number.isInteger(value)) {
        buffer.writeShort((tag | DATA_TYPE_INT << 10) & 65535);
        buffer.writeShort(4);
        buffer.writeInt(value);
      } else {
        buffer.writeShort((tag | DATA_TYPE_FLOAT << 10) & 65535);
        buffer.writeShort(4);
        buffer.writeFloat(value);
      }
    }
  }
  function applyHeader(buffer, apiLevel, tags) {
    if (apiLevel < 7) throw new Error("only apiLevel >= 7 is implemented");
    const ordered = [...tags].sort((a, b) => a[0] - b[0]);
    buffer.start(HEADER_OP);
    buffer.writeInt(MAJOR_VERSION | MAGIC_NUMBER);
    buffer.writeInt(MINOR_VERSION);
    buffer.writeInt(PATCH_VERSION);
    buffer.writeInt(ordered.length);
    writeMap(buffer, ordered);
  }

  // src/json2rc/Writer.ts
  var OP = {
    COMPONENT_VALUE: 150,
    MODIFIER_OFFSET: 221,
    ACCESSIBILITY_SEMANTICS: 250,
    MODIFIER_CLICK: 59,
    HOST_NAMED_ACTION: 210,
    HOST_ACTION: 209,
    HOST_ACTION_METADATA: 216,
    MODIFIER_WIDTH_IN: 231,
    MODIFIER_HEIGHT_IN: 232,
    ADD_MESH_2D: 104,
    DRAW_MESH_2D: 105,
    MATRIX_FROM_MESH_2D: 106,
    DEFINE_MESH_3D: 110,
    SET_CAMERA_3D: 111,
    MATRIX_3D_OP: 112,
    DRAW_MESH_3D: 113,
    PAINT_3D_STATE: 114,
    SET_LIGHTS_3D: 115,
    VECTOR_EXPRESSION: 116,
    MESH_EXPRESSION_3D: 117,
    SET_TEXTURE_3D: 118,
    MESH_PRIMITIVE_3D: 120,
    TOUCH_EXPRESSION: 157,
    MODIFIER_VISIBILITY: 211,
    LAYOUT_FLOW: 240,
    IMPULSE_START: 164,
    IMPULSE_PROCESS: 165,
    MODIFIER_BORDER: 107,
    MODIFIER_SCROLL: 226,
    LAYOUT_FIT_BOX: 176,
    DATA_BITMAP: 101,
    PARTICLE_DEFINE: 161,
    PARTICLE_LOOP: 163,
    PARTICLE_COMPARE: 194,
    VALUE_FLOAT_EXPRESSION_CHANGE_ACTION: 227,
    VALUE_FLOAT_CHANGE_ACTION: 222,
    VALUE_INTEGER_CHANGE_ACTION: 212,
    VALUE_STRING_CHANGE_ACTION: 213,
    VALUE_INTEGER_EXPRESSION_CHANGE_ACTION: 218,
    CONDITIONAL_OPERATIONS: 178,
    DATA_PATH: 123,
    DRAW_PATH: 124,
    PATH_CREATE: 159,
    PATH_EXPRESSION: 193,
    CLIP_PATH: 38,
    DRAW_TEXT_ON_PATH: 53,
    DATA_LIST_IDS: 146,
    TEXT_LOOKUP: 151,
    REM: 185,
    PATH_APPEND: 160,
    TEXT_FROM_FLOAT: 135,
    FLOAT_LIST: 147,
    CV_WIDTH: 0,
    CV_HEIGHT: 1,
    LAYOUT_ROOT: 200,
    LAYOUT_CONTENT: 201,
    LAYOUT_BOX: 202,
    LAYOUT_ROW: 203,
    LAYOUT_COLUMN: 204,
    LAYOUT_CANVAS: 205,
    LAYOUT_CANVAS_CONTENT: 207,
    CONTAINER_END: 214,
    DATA_TEXT: 102,
    CORE_TEXT: 239,
    MODIFIER_WIDTH: 16,
    MODIFIER_HEIGHT: 67,
    MODIFIER_BACKGROUND: 55,
    MODIFIER_PADDING: 58,
    MODIFIER_CLIP_RECT: 108,
    MODIFIER_ROUNDED_CLIP_RECT: 54,
    DATA_FLOAT: 80,
    ANIMATED_FLOAT: 81,
    COLOR_CONSTANT: 138,
    NAMED_VARIABLE: 137,
    PAINT_VALUES: 40,
    DRAW_RECT: 42,
    DRAW_CIRCLE: 46,
    DRAW_LINE: 47,
    DRAW_OVAL: 56,
    DRAW_ROUND_RECT: 51,
    DRAW_ARC: 152,
    DRAW_SECTOR: 52,
    DRAW_TEXT_ANCHOR: 133,
    MATRIX_SAVE: 130,
    MATRIX_RESTORE: 131,
    MATRIX_TRANSLATE: 127,
    MATRIX_SCALE: 126,
    MATRIX_ROTATE: 129,
    CLIP_RECT: 39,
    LOOP_START: 215
  };
  var TYPE_EXACT = 0;
  var TYPE_FILL = 1;
  var TYPE_WEIGHT = 3;
  var BITMAP_TYPE_PNG = 1;
  var BITMAP_ENCODING_INLINE = 0;
  var INT_MAX = 2147483647;
  var CORE_TEXT_PARAMS = [
    ["id", 1, "i", -1],
    ["animationId", 2, "i", -1],
    ["color", 3, "i", 4278190080],
    ["colorId", 4, "i", -1],
    ["fontSize", 5, "f", 36],
    ["minFontSize", 25, "f", -1],
    ["maxFontSize", 26, "f", -1],
    ["fontStyle", 6, "i", 0],
    ["fontWeight", 7, "f", 400],
    ["fontFamily", 8, "i", -1],
    ["textAlign", 9, "i", 1],
    ["overflow", 10, "i", 1],
    ["maxLines", 11, "i", INT_MAX],
    ["letterSpacing", 12, "f", 0],
    ["lineHeightAdd", 13, "f", 0],
    ["lineHeightMultiplier", 14, "f", 1],
    ["lineBreakStrategy", 15, "i", 0],
    ["hyphenationFrequency", 16, "i", 0],
    ["justificationMode", 17, "i", 0],
    ["underline", 18, "b", false],
    ["strikethrough", 19, "b", false],
    ["autosize", 22, "b", false],
    ["flags", 23, "i", 0],
    ["parentId", 24, "i", -1]
  ];
  function toInt32(v) {
    return v | 0;
  }
  var Writer = class {
    constructor(apiLevel, tags) {
      this.buffer = new WireBuffer();
      this.componentId = -1;
      // pre-decrement -> first is -2
      this.nextDataId = 42;
      // post-increment -> first is 42
      this.lastComponentId = 0;
      this.textCache = /* @__PURE__ */ new Map();
      this.componentValueCache = /* @__PURE__ */ new Map();
      this.tffCache = /* @__PURE__ */ new Map();
      this.nextArrayId = (2 << 20) + 42;
      // NanMap.START_ARRAY
      this.insertPoint = -1;
      this.globalStart = -1;
      applyHeader(this.buffer, apiLevel, tags);
    }
    encodeToByteArray() {
      return this.buffer.toBytes();
    }
    allocComponentId() {
      this.componentId -= 1;
      this.lastComponentId = this.componentId;
      return this.componentId;
    }
    allocDataId() {
      return this.nextDataId++;
    }
    // ── global sections, hoisted ahead of the root ───────────────────────────
    beginGlobal() {
      if (this.globalStart !== -1) throw new Error("global section started twice");
      if (this.insertPoint === -1) this.insertPoint = this.buffer.index;
      this.globalStart = this.buffer.index;
    }
    endGlobal() {
      if (this.globalStart === -1) throw new Error("global section ended without a begin");
      this.buffer.moveBlock(this.globalStart, this.insertPoint);
      this.insertPoint += this.buffer.index - this.globalStart;
      this.globalStart = -1;
    }
    // ── containers ──────────────────────────────────────────────────────────
    rootStart() {
      if (this.insertPoint === -1) this.insertPoint = this.buffer.index;
      this.buffer.start(OP.LAYOUT_ROOT);
      this.buffer.writeInt(this.allocComponentId());
    }
    containerEnd() {
      this.buffer.start(OP.CONTAINER_END);
    }
    contentStart() {
      this.buffer.start(OP.LAYOUT_CONTENT);
      this.buffer.writeInt(this.allocComponentId());
    }
    containerWithAlign(op, horizontal, vertical, spacedBy, modifiers) {
      const cid = this.allocComponentId();
      this.buffer.start(op);
      this.buffer.writeInt(cid);
      this.buffer.writeInt(-1);
      this.buffer.writeInt(horizontal);
      this.buffer.writeInt(vertical);
      if (spacedBy !== null) this.buffer.writeFloat(spacedBy);
      for (const m of modifiers) m(this);
      this.contentStart();
    }
    startColumn(h, v, mods, spacedBy = 0) {
      this.containerWithAlign(OP.LAYOUT_COLUMN, h, v, spacedBy, mods);
    }
    startRow(h, v, mods, spacedBy = 0) {
      this.containerWithAlign(OP.LAYOUT_ROW, h, v, spacedBy, mods);
    }
    startBox(h, v, mods) {
      this.containerWithAlign(OP.LAYOUT_BOX, h, v, null, mods);
    }
    endContainer() {
      this.containerEnd();
      this.containerEnd();
    }
    /**
     * A childless box: BoxStart, modifiers, one ContainerEnd.
     *
     * Not startBox+endBox with nothing between — that form also writes a content op and so
     * closes twice. Upstream also defaults a leaf's alignment to centre where the container
     * form defaults to start/top.
     */
    boxLeaf(h, v, mods) {
      const cid = this.allocComponentId();
      this.buffer.start(OP.LAYOUT_BOX);
      this.buffer.writeInt(cid);
      this.buffer.writeInt(-1);
      this.buffer.writeInt(h);
      this.buffer.writeInt(v);
      for (const m of mods) m(this);
      this.containerEnd();
    }
    /**
     * A canvas emits *both* a plain content op and, at apiLevel <= 7, a canvas-content op —
     * two component ids, not one. Emitting only the canvas-content op left every later id
     * short by one and the document six bytes light.
     */
    startCanvas(mods, apiLevel) {
      const cid = this.allocComponentId();
      this.buffer.start(OP.LAYOUT_CANVAS);
      this.buffer.writeInt(cid);
      this.buffer.writeInt(-1);
      for (const m of mods) m(this);
      this.contentStart();
      if (apiLevel <= 7) {
        this.buffer.start(OP.LAYOUT_CANVAS_CONTENT);
        this.buffer.writeInt(this.allocComponentId());
      }
    }
    /** Three ends at apiLevel <= 7 — one for the canvas-content op startCanvas added. */
    endCanvas(apiLevel) {
      if (apiLevel <= 7) this.containerEnd();
      this.containerEnd();
      this.containerEnd();
    }
    /**
     * Emit a FloatExpression (ANIMATED_FLOAT) and return its asNan(id) bits.
     *
     * The length field packs both counts: exprLen | (animLen << 16).
     */
    /**
     * Component WIDTH/HEIGHT of the *last started component*, cached per (component, type).
     *
     * This is why `lastComponentId` is tracked: `componentWidth()` does not mean the root's
     * width, it means whichever component is currently being built.
     */
    addComponentValue(valueType) {
      const key = `${this.lastComponentId}:${valueType}`;
      const hit = this.componentValueCache.get(key);
      if (hit !== void 0) return hit;
      const dataId = this.allocDataId();
      this.buffer.start(OP.COMPONENT_VALUE);
      this.buffer.writeInt(valueType);
      this.buffer.writeInt(this.lastComponentId);
      this.buffer.writeInt(dataId);
      const value = asNanBits(dataId);
      this.componentValueCache.set(key, value);
      return value;
    }
    floatExpression(opsBits, animFloats) {
      const dataId = this.allocDataId();
      this.buffer.start(OP.ANIMATED_FLOAT);
      this.buffer.writeInt(dataId);
      const nAnim = animFloats ? animFloats.length : 0;
      this.buffer.writeInt(opsBits.length | nAnim << 16);
      for (const b of opsBits) this.buffer.writeInt(b);
      if (animFloats) for (const v of animFloats) this.buffer.writeFloat(v);
      return asNanBits(dataId);
    }
    // ── paint + draw ────────────────────────────────────────────────────────
    paintValues(ints) {
      this.buffer.start(OP.PAINT_VALUES);
      this.buffer.writeInt(ints.length);
      for (const v of ints) this.buffer.writeInt(v);
    }
    /**
     * Canvas float fields go out as raw float32 *bits*, not through writeFloat, so a value
     * that is an expression result (a NaN-boxed id) survives. For a literal the bits are
     * identical either way.
     */
    op(code, bits) {
      this.buffer.start(code);
      for (const b of bits) this.buffer.writeInt(b);
    }
    drawRect(l, t, r, b) {
      this.op(OP.DRAW_RECT, [l, t, r, b]);
    }
    drawOval(l, t, r, b) {
      this.op(OP.DRAW_OVAL, [l, t, r, b]);
    }
    drawCircle(cx, cy, rad) {
      this.op(OP.DRAW_CIRCLE, [cx, cy, rad]);
    }
    drawLine(x1, y1, x2, y2) {
      this.op(OP.DRAW_LINE, [x1, y1, x2, y2]);
    }
    drawRoundRect(l, t, r, b, rx, ry) {
      this.op(OP.DRAW_ROUND_RECT, [l, t, r, b, rx, ry]);
    }
    drawArc(l, t, r, b, start, sweep) {
      this.op(OP.DRAW_ARC, [l, t, r, b, start, sweep]);
    }
    drawSector(l, t, r, b, start, sweep) {
      this.op(OP.DRAW_SECTOR, [l, t, r, b, start, sweep]);
    }
    drawTextAnchored(textId, x, y, panX, panY, flags) {
      this.buffer.start(OP.DRAW_TEXT_ANCHOR);
      this.buffer.writeInt(textId);
      for (const b of [x, y, panX, panY]) this.buffer.writeInt(b);
      this.buffer.writeInt(flags);
    }
    matrixSave() {
      this.buffer.start(OP.MATRIX_SAVE);
    }
    matrixRestore() {
      this.buffer.start(OP.MATRIX_RESTORE);
    }
    matrixTranslate(dx, dy) {
      this.op(OP.MATRIX_TRANSLATE, [dx, dy]);
    }
    /** Four fields: sx, sy and a pivot that defaults to NaN. Writing two lost 8 bytes. */
    matrixScale(sx, sy, px = 2143289344, py = 2143289344) {
      this.op(OP.MATRIX_SCALE, [sx, sy, px, py]);
    }
    matrixRotate(angle, px, py) {
      this.op(OP.MATRIX_ROTATE, [angle, px, py]);
    }
    clipRect(l, t, r, b) {
      this.op(OP.CLIP_RECT, [l, t, r, b]);
    }
    // ── paths ───────────────────────────────────────────────────────────────
    /**
     * A whole path as one DATA_PATH op. Verb tags are NaN-boxed ids, coordinates are plain
     * floats — so the array is a mix of raw bit patterns and numbers, distinguished here by
     * a marker rather than by type.
     */
    addPathData(items) {
      const pathId = this.allocDataId();
      this.buffer.start(OP.DATA_PATH);
      this.buffer.writeInt(pathId);
      this.buffer.writeInt(items.length);
      for (const v of items) {
        if (typeof v === "number") this.buffer.writeInt(floatToRawIntBits(v));
        else this.buffer.writeInt(v.bits);
      }
      return pathId;
    }
    pathCreate(xBits, yBits) {
      const pathId = this.allocDataId();
      this.buffer.start(OP.PATH_CREATE);
      this.buffer.writeInt(pathId);
      this.buffer.writeInt(xBits);
      this.buffer.writeInt(yBits);
      return pathId;
    }
    /**
     * Emit a PATH_EXPRESSION; return the path id.
     *
     * The path is sampled `count` times with the parameter swept from `min` to `max`, and
     * `a[0]` in either expression is that parameter. A null Y expression is written as a
     * zero-length array, which is how the reference marks "X only".
     *
     * min/max/count go out as float32 *bits* rather than as floats, because each may be a
     * NaN-boxed variable id - the same reason drawCircle takes bits.
     */
    pathExpression(flags, minBits, maxBits, countBits, xOps, yOps) {
      const pathId = this.allocDataId();
      this.buffer.start(OP.PATH_EXPRESSION);
      this.buffer.writeInt(pathId);
      this.buffer.writeInt(flags);
      for (const b of [minBits, maxBits, countBits]) this.buffer.writeInt(b);
      this.buffer.writeInt(xOps.length);
      for (const b of xOps) this.buffer.writeInt(b);
      this.buffer.writeInt(yOps ? yOps.length : 0);
      for (const b of yOps ?? []) this.buffer.writeInt(b);
      return pathId;
    }
    pathAppendLineTo(pathId, xBits, yBits) {
      this.buffer.start(OP.PATH_APPEND);
      this.buffer.writeInt(pathId);
      this.buffer.writeInt(5);
      this.buffer.writeInt(asNanBits(11));
      this.buffer.writeInt(floatToRawIntBits(0));
      this.buffer.writeInt(floatToRawIntBits(0));
      this.buffer.writeInt(xBits);
      this.buffer.writeInt(yBits);
    }
    pathAppendClose(pathId) {
      this.buffer.start(OP.PATH_APPEND);
      this.buffer.writeInt(pathId);
      this.buffer.writeInt(1);
      this.buffer.writeInt(asNanBits(15));
    }
    /** CLIP_PATH (38). */
    clipPath(pathId) {
      this.buffer.start(OP.CLIP_PATH);
      this.buffer.writeInt(pathId);
    }
    /**
     * DRAW_TEXT_ON_PATH (53). The offsets go on the wire **vOffset first**, which is the
     * reverse of the argument order everywhere else in this family — that is the
     * reference's own byte order, and getting it backwards is quiet: the text still draws,
     * just offset across the path instead of along it.
     */
    drawTextOnPath(textId, pathId, hBits, vBits) {
      this.buffer.start(OP.DRAW_TEXT_ON_PATH);
      this.buffer.writeInt(textId);
      this.buffer.writeInt(pathId);
      this.buffer.writeInt(vBits);
      this.buffer.writeInt(hBits);
    }
    /** DATA_LIST_IDS (146): a list of data ids — here text ids — that textLookup indexes. */
    addList(ids) {
      const listId = this.allocDataId();
      this.buffer.start(OP.DATA_LIST_IDS);
      this.buffer.writeInt(listId);
      this.buffer.writeInt(ids.length);
      for (const id of ids) this.buffer.writeInt(id);
      return listId;
    }
    /**
     * TEXT_LOOKUP (151): a text id whose string is `list[index]`, resolved live.
     * This is what lets one drawTextOnPath inside a loop print a different string each
     * iteration instead of needing N unrolled draws.
     */
    textLookup(dataSetId, indexBits) {
      const textId = this.allocDataId();
      this.buffer.start(OP.TEXT_LOOKUP);
      this.buffer.writeInt(textId);
      this.buffer.writeInt(dataSetId);
      this.buffer.writeInt(indexBits);
      return textId;
    }
    /** REM (185): a remark carried in the document. No visual effect. */
    rem(message) {
      this.buffer.start(OP.REM);
      this.buffer.writeUtf8(message);
    }
    drawPath(pathId) {
      this.buffer.start(OP.DRAW_PATH);
      this.buffer.writeInt(pathId);
    }
    /**
     * CONDITIONAL_OPERATIONS (178). The condition type is a *byte*, not an int — the two
     * float operands that follow are ints, so writing four bytes here desyncs everything
     * after it rather than producing a wrong comparison.
     */
    conditionalOperations(condType, aBits, bBits) {
      this.buffer.start(OP.CONDITIONAL_OPERATIONS);
      this.buffer.writeByte(condType);
      this.buffer.writeInt(aBits);
      this.buffer.writeInt(bBits);
    }
    endConditional() {
      this.containerEnd();
    }
    /** LAYOUT_FLOW (240). Closes with two ends, like the other containers. */
    startFlow(h, v, mods, maxItems, spacedBy = 0) {
      const cid = this.allocComponentId();
      this.buffer.start(OP.LAYOUT_FLOW);
      this.buffer.writeInt(cid);
      this.buffer.writeInt(-1);
      this.buffer.writeInt(h);
      this.buffer.writeInt(v);
      this.buffer.writeFloat(spacedBy);
      this.buffer.writeInt(maxItems);
      this.buffer.writeInt(INT_MAX);
      for (const m of mods) m(this);
      this.contentStart();
    }
    endFlow() {
      this.containerEnd();
      this.containerEnd();
    }
    /** LAYOUT_FIT_BOX (176). Two ends, like the other aligned containers. */
    startFitBox(h, v, mods) {
      const cid = this.allocComponentId();
      this.buffer.start(OP.LAYOUT_FIT_BOX);
      this.buffer.writeInt(cid);
      this.buffer.writeInt(-1);
      this.buffer.writeInt(h);
      this.buffer.writeInt(v);
      for (const m of mods) m(this);
      this.contentStart();
    }
    endFitBox() {
      this.containerEnd();
      this.containerEnd();
    }
    /**
     * Mirrors RemoteComposeWriter.addModifierScroll: reserve two ids for the scroll extent
     * and the notch extent, write the modifier, then a TouchExpression that *writes the
     * caller's position variable*, and close the container.
     *
     * The two reserved ids are never written to here — the player fills them in once it
     * knows the content size. Reserving them is still required, because their ids are baked
     * into the modifier and everything allocated afterwards depends on the count.
     */
    addModifierScroll(direction, positionBits) {
      const maxBits = asNanBits(this.allocDataId());
      const notchMaxBits = asNanBits(this.allocDataId());
      this.buffer.start(OP.MODIFIER_SCROLL);
      this.buffer.writeInt(direction);
      for (const b of [positionBits, maxBits, notchMaxBits]) {
        this.buffer.writeIntBitsAsFloat(b);
      }
      const touchId = direction === 0 ? 14 : 13;
      const mul = asNanBits(3211264 + 3);
      const exp = [asNanBits(touchId), floatToRawIntBits(-1), mul];
      this.touchExpression(
        floatToRawIntBits(0),
        // default
        floatToRawIntBits(0),
        // min
        maxBits,
        // max: the reserved extent
        0,
        // STOP_GENTLY
        floatToRawIntBits(0),
        // velocity id
        3,
        // touch effects
        exp,
        null,
        null,
        positionBits & 8388607
      );
      this.buffer.start(OP.CONTAINER_END);
    }
    /**
     * PARTICLE_DEFINE (161). Returns [systemId, one data id per variable].
     *
     * The id order is load-bearing: the system's own id is allocated first, then one per
     * variable, all before the op is written. Any other order still produces a readable
     * document in which every particle reads the wrong variable.
     */
    createParticles(initOps, particleCount) {
      const systemId = this.allocDataId();
      const varIds = initOps.map(() => this.allocDataId());
      this.buffer.start(OP.PARTICLE_DEFINE);
      this.buffer.writeInt(systemId);
      this.buffer.writeInt(particleCount);
      this.buffer.writeInt(varIds.length);
      varIds.forEach((vid, i) => {
        this.buffer.writeInt(vid);
        this.buffer.writeInt(initOps[i].length);
        for (const b of initOps[i]) this.buffer.writeInt(b);
      });
      return [systemId, varIds];
    }
    /** PARTICLE_LOOP (163) — a container; the body follows, then containerEnd(). */
    particlesLoopStart(systemId, restartOps, eqOps) {
      this.buffer.start(OP.PARTICLE_LOOP);
      this.buffer.writeInt(systemId);
      this.buffer.writeInt(restartOps ? restartOps.length : 0);
      for (const b of restartOps ?? []) this.buffer.writeInt(b);
      this.buffer.writeInt(eqOps.length);
      for (const ops of eqOps) {
        this.buffer.writeInt(ops.length);
        for (const b of ops) this.buffer.writeInt(b);
      }
    }
    /**
     * PARTICLE_COMPARE (194) — a container. `flags` is a *short* here, the one field in the
     * particle ops that is not an int, and an absent condition or equation list is a zero
     * count rather than an omission.
     */
    particlesCompareStart(systemId, flags, minBits, maxBits, condition, then1, then2) {
      this.buffer.start(OP.PARTICLE_COMPARE);
      this.buffer.writeInt(systemId);
      this.buffer.writeShort(flags);
      this.buffer.writeInt(minBits);
      this.buffer.writeInt(maxBits);
      this.buffer.writeInt(condition ? condition.length : 0);
      for (const b of condition ?? []) this.buffer.writeInt(b);
      for (const group of [then1, then2]) {
        this.buffer.writeInt(group ? group.length : 0);
        for (const eq of group ?? []) {
          this.buffer.writeInt(eq.length);
          for (const b of eq) this.buffer.writeInt(b);
        }
      }
    }
    /** IMPULSE_START (164) — a container; close it with containerEnd(). */
    impulseStart(durationBits, startBits) {
      this.buffer.start(OP.IMPULSE_START);
      this.buffer.writeInt(durationBits);
      this.buffer.writeInt(startBits);
    }
    /** IMPULSE_PROCESS (165) — a container with no payload. */
    impulseProcessStart() {
      this.buffer.start(OP.IMPULSE_PROCESS);
    }
    // ── bitmaps ─────────────────────────────────────────────────────────────
    /**
     * DATA_BITMAP (101), the packed form.
     *
     * Width and height each share a word with a second field — type in the high half of the
     * first, encoding in the high half of the second. The reader tells this from the plain
     * form by those high bits being set, which is why each dimension must stay inside 16
     * bits: a 70000-pixel image would silently read back as a different type.
     */
    addBitmap(imageId, width, height, data, type = BITMAP_TYPE_PNG, encoding = BITMAP_ENCODING_INLINE) {
      if (!(width >= 0 && width <= 65535) || !(height >= 0 && height <= 65535)) {
        throw new Error(
          `bitmap ${width}x${height}: each dimension must fit in 16 bits, because the wire format packs type and encoding into the high half of those words`
        );
      }
      this.buffer.start(OP.DATA_BITMAP);
      this.buffer.writeInt(imageId);
      this.buffer.writeInt((type & 65535) << 16 | width & 65535);
      this.buffer.writeInt((encoding & 65535) << 16 | height & 65535);
      this.buffer.writeBuffer(data);
      return imageId;
    }
    // ── 3D ──────────────────────────────────────────────────────────────────
    //
    // Every 3D payload is a length-prefixed array of raw 32-bit patterns. They are bit
    // patterns rather than floats because an element may be a NaN-boxed variable id, and
    // writing one through a float would destroy the payload.
    bitsArray(bits) {
      this.buffer.writeInt(bits.length);
      for (const b of bits) this.buffer.writeInt(b);
    }
    // 2D vertex mesh (ADD_MESH_2D, type TYPE_VALUES=1): explicit triangles. verts is a flat
    // [x,y,...] list, colors one ARGB int per vertex, indices a triangle list. uv optional (for
    // a textured draw). Matches AddMesh2D::read in rccore: verts/uv as floats, indices as
    // unsigned shorts, colors as ints.
    addMesh2DValues(meshId, verts, colors, indices, uv = []) {
      this.buffer.start(OP.ADD_MESH_2D);
      this.buffer.writeInt(meshId);
      this.buffer.writeInt(1);   // TYPE_VALUES
      this.buffer.writeInt(0);   // layout (unused for explicit values)
      this.buffer.writeInt(0);   // uCount
      this.buffer.writeInt(0);   // vCount
      this.buffer.writeInt(0);   // flags
      this.buffer.writeInt(0);   // aux
      this.buffer.writeInt(indices.length);
      for (const i of indices) this.buffer.writeShort(i & 0xffff);
      this.buffer.writeInt(verts.length);
      this.buffer.writeInt(uv.length);
      this.buffer.writeInt(colors.length);
      for (const v of verts) this.buffer.writeFloat(v);
      for (const u of uv) this.buffer.writeFloat(u);
      for (const c of colors) this.buffer.writeInt(c | 0);
    }
    drawMesh2D(meshId, blend, imageId) {
      this.buffer.start(OP.DRAW_MESH_2D);
      this.buffer.writeInt(meshId);
      this.buffer.writeInt(blend);
      this.buffer.writeInt(imageId);
    }
    defineMesh3D(meshId, indices, verts, normals, uv) {
      this.buffer.start(OP.DEFINE_MESH_3D);
      this.buffer.writeInt(meshId);
      this.buffer.writeInt(indices.length);
      for (const i of indices) this.buffer.writeInt(i);
      this.bitsArray(verts);
      this.bitsArray(normals ?? []);
      this.bitsArray(uv ?? []);
    }
    setCamera3D(projection, projParams, viewParams) {
      this.buffer.start(OP.SET_CAMERA_3D);
      this.buffer.writeInt(projection);
      this.bitsArray(projParams);
      this.bitsArray(viewParams);
    }
    matrix3DOp(sub, args) {
      this.buffer.start(OP.MATRIX_3D_OP);
      this.buffer.writeInt(sub);
      this.bitsArray(args);
    }
    drawMesh3D(meshId, mode) {
      this.buffer.start(OP.DRAW_MESH_3D);
      this.buffer.writeInt(meshId);
      this.buffer.writeInt(mode);
    }
    paint3DState(sub, params) {
      this.buffer.start(OP.PAINT_3D_STATE);
      this.buffer.writeInt(sub);
      this.bitsArray(params);
    }
    setLights3D(types, colors, params) {
      this.buffer.start(OP.SET_LIGHTS_3D);
      this.buffer.writeInt(types.length);
      for (let i = 0; i < types.length; i++) {
        this.buffer.writeInt(types[i]);
        this.buffer.writeInt(colors[i]);
      }
      this.bitsArray(params);
    }
    setTexture3D(bitmapId) {
      this.buffer.start(OP.SET_TEXTURE_3D);
      this.buffer.writeInt(bitmapId);
    }
    meshPrimitive3D(meshId, ptype, segments, flags, channels) {
      this.buffer.start(OP.MESH_PRIMITIVE_3D);
      this.buffer.writeInt(meshId);
      this.buffer.writeInt(ptype);
      this.buffer.writeInt(segments);
      this.buffer.writeInt(flags);
      this.buffer.writeInt(channels.length);
      for (const ch of channels) this.bitsArray(ch);
    }
    /** MESH_EXPRESSION_3D: RPN expressions the player evaluates over a (u,v) grid. */
    meshExpression3D(meshId, mtype, flags, params, pos, normal, uv) {
      this.buffer.start(OP.MESH_EXPRESSION_3D);
      this.buffer.writeInt(meshId);
      this.buffer.writeInt(mtype);
      this.buffer.writeInt(flags);
      this.bitsArray(params);
      for (const group of [pos, normal, uv]) {
        this.buffer.writeInt(group.length);
        for (const e of group) this.bitsArray(e);
      }
    }
    /**
     * VECTOR_EXPRESSION. Returns the base data id.
     *
     * The components land in consecutive ids base..base+dimension-1, so the ids after the
     * base are reserved here — handing one to another op would silently overwrite a
     * component every frame.
     */
    vectorExpression(dimension, flags, opsBits) {
      const base = this.allocDataId();
      for (let i = 0; i < dimension - 1; i++) this.allocDataId();
      this.buffer.start(OP.VECTOR_EXPRESSION);
      this.buffer.writeInt(base);
      this.buffer.writeByte(dimension);
      this.buffer.writeByte(flags);
      this.buffer.writeShort(opsBits.length);
      for (const b of opsBits) this.buffer.writeInt(b);
      return base;
    }
    /**
     * TOUCH_EXPRESSION. Returns the asNan(id) bits.
     *
     * Unlike a float expression the value *accumulates*: the expression maps a touch to a
     * delta, and the op integrates it, clamps to [min, max] and coasts on release. The four
     * leading fields go out as raw bits because each may be a NaN-boxed id, and because NaN
     * is a legitimate value here — an absent `min` means wrap around `max`, which is a
     * different behaviour from min=0.
     */
    touchExpression(defaultBits, minBits, maxBits, touchMode, velocityBits, touchEffects, expBits, touchSpec, easingSpec, dataIdIn) {
      const dataId = dataIdIn ?? this.allocDataId();
      this.buffer.start(OP.TOUCH_EXPRESSION);
      this.buffer.writeInt(dataId);
      for (const b of [defaultBits, minBits, maxBits, velocityBits]) {
        this.buffer.writeIntBitsAsFloat(b);
      }
      this.buffer.writeInt(touchEffects);
      this.buffer.writeInt(expBits.length);
      for (const b of expBits) this.buffer.writeIntBitsAsFloat(b);
      const spec = touchSpec ?? [];
      this.buffer.writeInt((touchMode & 65535) << 16 | spec.length & 65535);
      for (const v of spec) this.buffer.writeFloat(v);
      const easing = easingSpec ?? [];
      this.buffer.writeInt(easing.length);
      for (const v of easing) this.buffer.writeFloat(v);
      return asNanBits(dataId);
    }
    // ── control flow ────────────────────────────────────────────────────────
    startLoop(indexId, fromBits, stepBits, untilBits) {
      this.buffer.start(OP.LOOP_START);
      this.buffer.writeInt(indexId);
      for (const b of [fromBits, stepBits, untilBits]) this.buffer.writeInt(b);
    }
    endLoop() {
      this.containerEnd();
    }
    // ── data ────────────────────────────────────────────────────────────────
    addText(s) {
      const hit = this.textCache.get(s);
      if (hit !== void 0) return hit;
      const id = this.allocDataId();
      this.buffer.start(OP.DATA_TEXT);
      this.buffer.writeInt(id);
      this.buffer.writeUtf8(s);
      this.textCache.set(s, id);
      return id;
    }
    /**
     * A string the host can read and write by name: NAMED_VARIABLE (STRING_TYPE 0), then
     * DATA_TEXT — RemoteComposeWriter.addNamedString. Not interned — two variables with the same initial text must still
     * be two ids, or setting one would set both. EXTENSION: not in the reference JSON
     * parser yet (m5Tab docs/RC_SYSTEM_VARIABLES.md).
     */
    addNamedString(name, value) {
      const id = this.allocDataId();
      this.buffer.start(OP.NAMED_VARIABLE);
      this.buffer.writeInt(id);
      this.buffer.writeInt(0);
      this.buffer.writeUtf8(name);
      this.buffer.start(OP.DATA_TEXT);
      this.buffer.writeInt(id);
      this.buffer.writeUtf8(value);
      return id;
    }
    /** FloatConstant (DATA_FLOAT); returns asNan(id) bits. */
    addFloatConstant(value) {
      const dataId = this.allocDataId();
      this.buffer.start(OP.DATA_FLOAT);
      this.buffer.writeInt(dataId);
      this.buffer.writeFloat(value);
      return asNanBits(dataId);
    }
    /**
     * A float constant the host can read and write by name.
     *
     * The NAMED_VARIABLE goes *before* the constant — the opposite order from a named float
     * array, which writes the array first. Not an inconsistency to tidy: it mirrors upstream,
     * and swapping either changes the bytes.
     */
    addNamedFloat(name, value) {
      const dataId = this.allocDataId();
      this.buffer.start(OP.NAMED_VARIABLE);
      this.buffer.writeInt(dataId);
      this.buffer.writeInt(1);
      this.buffer.writeUtf8(name);
      this.buffer.start(OP.DATA_FLOAT);
      this.buffer.writeInt(dataId);
      this.buffer.writeFloat(value);
      return asNanBits(dataId);
    }
    setFloatName(dataId, name) {
      this.buffer.start(OP.NAMED_VARIABLE);
      this.buffer.writeInt(dataId);
      this.buffer.writeInt(1);
      this.buffer.writeUtf8(name);
    }
    /** COLOR_CONSTANT; returns the raw colour id (NOT NaN-boxed). */
    addColor(color) {
      const colorId = this.allocDataId();
      this.buffer.start(OP.COLOR_CONSTANT);
      this.buffer.writeInt(colorId);
      this.buffer.writeInt(color);
      return colorId;
    }
    addNamedColor(name, color) {
      const colorId = this.addColor(color);
      this.buffer.start(OP.NAMED_VARIABLE);
      this.buffer.writeInt(colorId);
      this.buffer.writeInt(2);
      this.buffer.writeUtf8(name);
      return colorId;
    }
    /** FLOAT_LIST. Arrays have their own id counter, starting at START_ARRAY. */
    addFloatArray(values) {
      const arrayId = this.nextArrayId++;
      this.buffer.start(OP.FLOAT_LIST);
      this.buffer.writeInt(arrayId);
      this.buffer.writeInt(values.length);
      for (const v of values) this.buffer.writeFloat(v);
      return arrayId;
    }
    /** Unlike addNamedFloat, the NAMED_VARIABLE comes *after* the array. */
    addNamedFloatArray(name, values) {
      const arrayId = this.addFloatArray(values);
      this.buffer.start(OP.NAMED_VARIABLE);
      this.buffer.writeInt(arrayId);
      this.buffer.writeInt(6);
      this.buffer.writeUtf8(name);
      return arrayId;
    }
    /** TEXT_FROM_FLOAT: a text id whose string is a live float, deduped like addText. */
    addTextFromFloat(valueBits, before, after, flags) {
      const key = `${valueBits}:${before}:${after}:${flags}`;
      const hit = this.tffCache.get(key);
      if (hit !== void 0) return hit;
      const dataId = this.allocDataId();
      this.buffer.start(OP.TEXT_FROM_FLOAT);
      this.buffer.writeInt(dataId);
      this.buffer.writeIntBitsAsFloat(valueBits);
      this.buffer.writeInt((before & 65535) << 16 | after & 65535);
      this.buffer.writeInt(flags);
      this.tffCache.set(key, dataId);
      return dataId;
    }
    // ── text component ──────────────────────────────────────────────────────
    textComponent(textId, p) {
      const cid = this.allocComponentId();
      const fb = floatToRawIntBits;
      const values = {
        id: cid,
        animationId: -1,
        color: p.color,
        colorId: p.colorId,
        fontSize: p.fontSizeBits,
        minFontSize: fb(-1),
        maxFontSize: fb(-1),
        fontStyle: 0,
        fontWeight: p.fontWeightBits,
        fontFamily: -1,
        textAlign: p.textAlign,
        overflow: p.overflow,
        maxLines: p.maxLines,
        letterSpacing: fb(0),
        lineHeightAdd: fb(0),
        lineHeightMultiplier: fb(1),
        lineBreakStrategy: 0,
        hyphenationFrequency: 0,
        justificationMode: 0,
        underline: false,
        strikethrough: false,
        autosize: false,
        flags: 0,
        parentId: -1
      };
      const emit = [];
      for (const [name, tag, typ, dflt] of CORE_TEXT_PARAMS) {
        const v = values[name];
        if (typ === "f") {
          if (v !== fb(dflt)) emit.push([tag, typ, v]);
        } else if (typ === "b") {
          if (Boolean(v) !== Boolean(dflt)) emit.push([tag, typ, v]);
        } else {
          if (toInt32(v) !== toInt32(dflt)) emit.push([tag, typ, v]);
        }
      }
      this.buffer.start(OP.CORE_TEXT);
      this.buffer.writeInt(textId);
      this.buffer.writeShort(emit.length);
      for (const [tag, typ, value] of emit) {
        this.buffer.writeByte(tag);
        if (typ === "b") this.buffer.writeByte(value ? 1 : 0);
        else this.buffer.writeInt(value);
      }
      for (const m of p.modifiers) m(this);
      this.contentStart();
      this.containerEnd();
      this.containerEnd();
    }
  };
  function modWidth(typeOrdinal, valueBits) {
    return (w) => {
      w.buffer.start(OP.MODIFIER_WIDTH);
      w.buffer.writeInt(typeOrdinal);
      w.buffer.writeInt(valueBits);
    };
  }
  function modHeight(typeOrdinal, valueBits) {
    return (w) => {
      w.buffer.start(OP.MODIFIER_HEIGHT);
      w.buffer.writeInt(typeOrdinal);
      w.buffer.writeInt(valueBits);
    };
  }
  function modPadding(l, t, r, b) {
    return (w) => {
      w.buffer.start(OP.MODIFIER_PADDING);
      for (const v of [l, t, r, b]) w.buffer.writeInt(v);
    };
  }
  function modBorder(width, corner, color, shape) {
    const a = (color >>> 24 & 255) / 255;
    const r = (color >>> 16 & 255) / 255;
    const g = (color >>> 8 & 255) / 255;
    const b = (color & 255) / 255;
    return (w) => {
      w.buffer.start(OP.MODIFIER_BORDER);
      for (let i = 0; i < 4; i++) w.buffer.writeInt(0);
      w.buffer.writeFloat(width);
      w.buffer.writeFloat(corner);
      w.buffer.writeFloat(r);
      w.buffer.writeFloat(g);
      w.buffer.writeFloat(b);
      w.buffer.writeFloat(a);
      w.buffer.writeInt(shape);
    };
  }
  function modScroll(direction, positionBits) {
    return (w) => w.addModifierScroll(direction, positionBits);
  }
  function modVisibility(valueId) {
    return (w) => {
      w.buffer.start(OP.MODIFIER_VISIBILITY);
      w.buffer.writeInt(valueId);
    };
  }
  function modClipRect() {
    return (w) => {
      w.buffer.start(OP.MODIFIER_CLIP_RECT);
    };
  }
  function modClipRoundedRect(topStart, topEnd, bottomStart, bottomEnd) {
    return (w) => {
      w.buffer.start(OP.MODIFIER_ROUNDED_CLIP_RECT);
      for (const v of [topStart, topEnd, bottomStart, bottomEnd]) w.buffer.writeFloat(v);
    };
  }
  function modWidthIn(minBits, maxBits) {
    return (w) => {
      w.buffer.start(OP.MODIFIER_WIDTH_IN);
      w.buffer.writeInt(minBits);
      w.buffer.writeInt(maxBits);
    };
  }
  function modHeightIn(minBits, maxBits) {
    return (w) => {
      w.buffer.start(OP.MODIFIER_HEIGHT_IN);
      w.buffer.writeInt(minBits);
      w.buffer.writeInt(maxBits);
    };
  }
  function modSemantics(contentDescriptionId, role, textId, stateDescriptionId, mode, enabled, clickable) {
    return (w) => {
      w.buffer.start(OP.ACCESSIBILITY_SEMANTICS);
      w.buffer.writeInt(contentDescriptionId);
      w.buffer.writeByte(role & 255);
      w.buffer.writeInt(textId);
      w.buffer.writeInt(stateDescriptionId);
      w.buffer.writeByte(mode);
      w.buffer.writeBoolean(enabled);
      w.buffer.writeBoolean(clickable);
    };
  }
  var SEMANTICS_MODE_SET = 0;
  function modClick(actions, clickType = 0) {
    return (w) => {
      w.buffer.start(OP.MODIFIER_CLICK);
      if (clickType !== 0) w.buffer.writeInt(clickType);
      for (const a of actions) a(w);
      w.buffer.start(OP.CONTAINER_END);
    };
  }
  function hostNamedAction(textId, type, valueId) {
    return (w) => {
      w.buffer.start(OP.HOST_NAMED_ACTION);
      w.buffer.writeInt(textId);
      w.buffer.writeInt(type);
      w.buffer.writeInt(valueId);
    };
  }
  var HOST_ACTION_STRING_TYPE = 2;
  function hostAction(actionId, metadataId) {
    return (w) => {
      if (metadataId === -1) {
        w.buffer.start(OP.HOST_ACTION);
        w.buffer.writeInt(actionId);
      } else {
        w.buffer.start(OP.HOST_ACTION_METADATA);
        w.buffer.writeInt(actionId);
        w.buffer.writeInt(metadataId);
      }
    };
  }
  function valueFloatExpressionChange(targetId, valueId) {
    return (w) => {
      w.buffer.start(OP.VALUE_FLOAT_EXPRESSION_CHANGE_ACTION);
      w.buffer.writeInt(targetId);
      w.buffer.writeInt(valueId);
    };
  }
  function valueFloatChange(targetId, valueBits) {
    return (w) => {
      w.buffer.start(OP.VALUE_FLOAT_CHANGE_ACTION);
      w.buffer.writeInt(targetId);
      w.buffer.writeInt(valueBits);
    };
  }
  function valueIntegerChange(targetId, value) {
    return (w) => {
      w.buffer.start(OP.VALUE_INTEGER_CHANGE_ACTION);
      w.buffer.writeInt(targetId);
      w.buffer.writeInt(value);
    };
  }
  function valueStringChange(targetId, textId) {
    return (w) => {
      w.buffer.start(OP.VALUE_STRING_CHANGE_ACTION);
      w.buffer.writeInt(targetId);
      w.buffer.writeInt(textId);
    };
  }
  function valueIntegerExpressionChange(targetId, valueId) {
    return (w) => {
      w.buffer.start(OP.VALUE_INTEGER_EXPRESSION_CHANGE_ACTION);
      w.buffer.writeLong(targetId);
      w.buffer.writeLong(valueId);
    };
  }
  function modOffset(xBits, yBits) {
    return (w) => {
      w.buffer.start(OP.MODIFIER_OFFSET);
      w.buffer.writeInt(xBits);
      w.buffer.writeInt(yBits);
    };
  }
  function modBackgroundId(colorId) {
    return (w) => {
      w.buffer.start(OP.MODIFIER_BACKGROUND);
      w.buffer.writeInt(2);
      w.buffer.writeInt(colorId);
      w.buffer.writeInt(0);
      w.buffer.writeInt(0);
      for (let i = 0; i < 4; i++) w.buffer.writeFloat(0);
      w.buffer.writeInt(0);
    };
  }
  function modBackground(color) {
    const a = (color >>> 24 & 255) / 255;
    const r = (color >>> 16 & 255) / 255;
    const g = (color >>> 8 & 255) / 255;
    const b = (color & 255) / 255;
    return (w) => {
      w.buffer.start(OP.MODIFIER_BACKGROUND);
      w.buffer.writeInt(0);
      w.buffer.writeInt(0);
      w.buffer.writeInt(0);
      w.buffer.writeInt(0);
      w.buffer.writeFloat(r);
      w.buffer.writeFloat(g);
      w.buffer.writeFloat(b);
      w.buffer.writeFloat(a);
      w.buffer.writeInt(0);
    };
  }

  // src/json2rc/Expr.ts
  var ExpressionError = class extends Error {
  };
  var OFFSET = 3211264;
  var RAND_SEED_OP = 40;
  var OPERATORS = { "+": 1, "-": 2, "*": 3, "/": 4, "%": 5, "u-": 73 };
  var PRECEDENCE = { "+": 1, "-": 1, "*": 2, "/": 2, "%": 2, "u-": 3 };
  var FUNCTIONS = { "seed": null, "seed_2arg": null, "sin": 18, "cos": 19, "tan": 20, "asin": 21, "acos": 22, "atan": 23, "atan2": 24, "sqrt": 9, "abs": 10, "pow": 8, "min": 6, "max": 7, "floor": 14, "ceil": 31, "log": 15, "ln": 16, "sign": 11, "round": 17, "lerp": 49, "step": 44, "smooth_step": 50, "clamp": 27, "ifElse": 26, "mad": 25, "ping_pong": 54, "fract": 53, "exp": 13, "hypot": 47, "square": 45, "rand": 39, "arrayMin": 34, "arrayMax": 33, "arrayLength": 37, "arraySum": 35, "arraySumSqr": 78, "arraySumXY": 77, "arrayGet": 32, "spline": 38, "arraySpline": 38, "splineLoop": 75, "anim": 256 };
  var SYSTEM_VARS = { "time": 1, "continuousSec": 1, "continuousSec()": 1, "seconds": 2, "timeInSec": 2, "timeInSec()": 2, "timeInMin": 3, "timeInMin()": 3, "timeInHr": 4, "timeInHr()": 4, "animationTime": 30, "animationTime()": 30, "animTime": 30, "deltaTime": 31, "deltaTime()": 31, "delta_time": 31, "dt": 31, "animationDeltaTime": 31, "animationDeltaTime()": 31, "epochSecond": 32, "epochSecond()": 32, "calendarMonth": 9, "calendarMonth()": 9, "month": 9, "offsetToUtc": 10, "offsetToUtc()": 10, "utcOffset": 10, "weekDay": 11, "weekDay()": 11, "weekday": 11, "dayOfWeek": 11, "dayOfMonth": 12, "dayOfMonth()": 12, "day": 12, "dayOfYear": 34, "dayOfYear()": 34, "year": 35, "year()": 35, "windowWidth": 5, "windowWidth()": 5, "windowHeight": 6, "windowHeight()": 6, "density": 27, "density()": 27, "apiLevel": 28, "apiLevel()": 28, "fontSize": 33, "fontSize()": 33, "touchX": 13, "touchX()": 13, "touchPosX": 13, "touchPositionX": 13, "touchY": 14, "touchY()": 14, "touchPosY": 14, "touchPositionY": 14, "touchVelX": 15, "touchVelX()": 15, "touchVelocityX": 15, "touchVelY": 16, "touchVelY()": 16, "touchVelocityY": 16, "touchTime": 29, "touchTime()": 29, "touchEventTime": 29, "touchEventTime()": 29, "accelX": 17, "accelX()": 17, "accelerationX": 17, "accelerationX()": 17, "accelY": 18, "accelY()": 18, "accelerationY": 18, "accelerationY()": 18, "accelZ": 19, "accelZ()": 19, "accelerationZ": 19, "accelerationZ()": 19, "gyroX": 20, "gyroX()": 20, "gyroRotX": 20, "gyroRotX()": 20, "gyroRotationX": 20, "gyroY": 21, "gyroY()": 21, "gyroRotY": 21, "gyroRotY()": 21, "gyroRotationY": 21, "gyroZ": 22, "gyroZ()": 22, "gyroRotZ": 22, "gyroRotZ()": 22, "gyroRotationZ": 22, "magneticX": 23, "magneticX()": 23, "magneticY": 24, "magneticY()": 24, "magneticZ": 25, "magneticZ()": 25, "light": 26, "light()": 26, "lightLevel": 26, "batteryLevel": 38, "batteryLevel()": 38, "powerState": 39, "powerState()": 39, "memoryFree": 40, "memoryFree()": 40, "networkSignal": 41, "networkSignal()": 41, "rand": 3211303, "rand()": 3211303, "a[0]": 3211334, "a[1]": 3211335, "a[2]": 3211336 };
  function isVariableRef(s) {
    return s.length >= 2 && (s.startsWith("$") || s.startsWith("@"));
  }
  function variableNameFromRef(s) {
    if (s.startsWith("$vars.") || s.startsWith("@vars.")) return s.slice(6);
    return s.slice(1);
  }
  var NUMBER_RE = /^[+-]?(\d+\.?\d*|\.\d+)([eE][+-]?\d+)?$/;
  var COMPONENT_WIDTH_VARS = /* @__PURE__ */ new Set(["width", "componentWidth", "componentWidth()"]);
  var COMPONENT_HEIGHT_VARS = /* @__PURE__ */ new Set(["height", "componentHeight", "componentHeight()"]);
  var ExpressionParser = class {
    constructor(writer, variables) {
      this.writer = writer;
      this.variables = variables;
    }
    parseExpression(value) {
      if (typeof value === "string") {
        return this.writer.floatExpression(this.infixToRpn(value));
      }
      if (value && typeof value === "object") {
        const ops = this.infixToRpn(String(value.value));
        const duration = Number(value.anim ?? 1);
        return this.writer.floatExpression(ops, duration !== 1 ? [duration] : null);
      }
      throw new ExpressionError(`cannot compile expression ${JSON.stringify(value)}`);
    }
    isVariable(token) {
      if (COMPONENT_WIDTH_VARS.has(token) || COMPONENT_HEIGHT_VARS.has(token)) return true;
      if (token in SYSTEM_VARS) return true;
      const name = isVariableRef(token) ? variableNameFromRef(token) : token;
      return this.variables.has(name);
    }
    variableNanBits(token) {
      if (COMPONENT_WIDTH_VARS.has(token)) return this.writer.addComponentValue(0);
      if (COMPONENT_HEIGHT_VARS.has(token)) return this.writer.addComponentValue(1);
      if (token in SYSTEM_VARS) return asNanBits(SYSTEM_VARS[token]);
      const name = isVariableRef(token) ? variableNameFromRef(token) : token;
      const bits = this.variables.get(name);
      if (bits === void 0) throw new ExpressionError(`Unknown variable: ${token}`);
      return bits;
    }
    isNumber(token) {
      return NUMBER_RE.test(token);
    }
    tokenize(expression) {
      const tokens = [];
      let sb = "";
      for (let i = 0; i < expression.length; i++) {
        const c = expression[i];
        if (/\s/.test(c)) continue;
        if (/[A-Za-z0-9]/.test(c) || "_.$@[]".includes(c)) {
          sb += c;
        } else if (c === "(") {
          if (i + 1 < expression.length && expression[i + 1] === ")") {
            sb += "()";
            i++;
          } else {
            if (sb) {
              tokens.push(sb);
              sb = "";
            }
            tokens.push("(");
          }
        } else {
          if (sb) {
            tokens.push(sb);
            sb = "";
          }
          tokens.push(c);
        }
      }
      if (sb) tokens.push(sb);
      const merged = [];
      for (let i = 0; i < tokens.length; i++) {
        const t = tokens[i];
        if (t === "-" && i + 1 < tokens.length && this.isNumber(tokens[i + 1])) {
          const prev = merged.length ? merged[merged.length - 1] : null;
          const unary = prev === null || prev === "(" || prev === "," || prev in OPERATORS;
          if (unary) {
            merged.push("-" + tokens[i + 1]);
            i++;
            continue;
          }
        }
        merged.push(t);
      }
      return merged;
    }
    infixToRpn(expression, extraFunctions) {
      const FUNCS = extraFunctions ? { ...FUNCTIONS, ...extraFunctions } : FUNCTIONS;
      const output = [];
      const stack = [];
      let lastWasOperator = true;
      const emitOp = (op) => {
        if (op in OPERATORS) output.push(asNanBits(OFFSET + OPERATORS[op]));
        else if (op in FUNCS && FUNCS[op] !== null) {
          output.push(asNanBits(OFFSET + FUNCS[op]));
        }
      };
      for (const token of this.tokenize(expression)) {
        if (this.isNumber(token)) {
          output.push(floatToRawIntBits(parseFloat(token)));
          lastWasOperator = false;
        } else if (this.isVariable(token)) {
          output.push(this.variableNanBits(token));
          lastWasOperator = false;
        } else if (token in FUNCS) {
          stack.push(token);
          lastWasOperator = true;
        } else if (token === ",") {
          while (stack.length && stack[stack.length - 1] !== "(") emitOp(stack.pop());
          if (stack.length >= 2 && stack[stack.length - 1] === "(" && stack[stack.length - 2] === "seed") {
            output.push(asNanBits(OFFSET + RAND_SEED_OP));
            stack[stack.length - 2] = "seed_2arg";
          }
          lastWasOperator = true;
        } else if (token in OPERATORS || token === "-") {
          if (token === "-" && lastWasOperator) {
            stack.push("u-");
          } else {
            while (stack.length && stack[stack.length - 1] in OPERATORS) {
              const p1 = PRECEDENCE[stack[stack.length - 1]] ?? 0;
              const p2 = PRECEDENCE[token] ?? 0;
              if (p1 > p2 || p1 === p2 && token !== "u-") emitOp(stack.pop());
              else break;
            }
            stack.push(token);
          }
          lastWasOperator = true;
        } else if (token === "(") {
          stack.push(token);
          lastWasOperator = true;
        } else if (token === ")") {
          while (stack.length && stack[stack.length - 1] !== "(") emitOp(stack.pop());
          if (!stack.length) throw new ExpressionError("Mismatched parentheses");
          stack.pop();
          if (stack.length && stack[stack.length - 1] in FUNCS) {
            const fn = stack.pop();
            if (fn === "seed") {
              output.push(asNanBits(OFFSET + RAND_SEED_OP));
              output.push(floatToRawIntBits(1));
            } else if (fn !== "seed_2arg") emitOp(fn);
          }
          lastWasOperator = false;
        } else {
          throw new ExpressionError(`Unknown token in expression: ${token}`);
        }
      }
      while (stack.length) {
        const op = stack.pop();
        if (op === "seed") {
          output.push(asNanBits(OFFSET + RAND_SEED_OP));
          output.push(floatToRawIntBits(1));
        } else if (op !== "seed_2arg") emitOp(op);
      }
      return output;
    }
  };

  // src/json2rc/Parser.ts
  var NotImplementedComponent = class extends Error {
    // An empty `extends Error` inherits `name` from Error.prototype, so `e.name` reads
    // "Error" and a deliberate refusal is indistinguishable from a crash. The harness
    // splits those two buckets on exactly this field.
    constructor(message) {
      super(message);
      this.name = "NotImplementedComponent";
    }
  };
  var DeferredVariables = class {
    constructor() {
      this.materialised = /* @__PURE__ */ new Map();
      this.deferred = /* @__PURE__ */ new Map();
      this.resolver = null;
    }
    defer(name, command) {
      this.deferred.set(name, command);
    }
    /** Only variables already written — a deferred entry is invisible here, which is what
     *  lets a later declaration of the same name replace a pending one. */
    isMaterialised(name) {
      return this.materialised.has(name);
    }
    set(name, bits) {
      this.materialised.set(name, bits);
    }
    remove(name) {
      this.materialised.delete(name);
      this.deferred.delete(name);
    }
    has(name) {
      return this.materialised.has(name) || this.deferred.has(name);
    }
    get(name) {
      const hit = this.materialised.get(name);
      if (hit !== void 0) return hit;
      const pending = this.deferred.get(name);
      if (pending === void 0 || !this.resolver) return void 0;
      this.deferred.delete(name);
      const bits = this.resolver(name, pending);
      this.materialised.set(name, bits);
      return bits;
    }
  };
  function toInt322(v) {
    return v | 0;
  }
  function parseColor(value) {
    if (typeof value === "number") return toInt322(value);
    if (typeof value === "string") {
      let s = value.trim();
      if (s.startsWith("#")) s = s.slice(1);
      let v = parseInt(s, 16);
      if (s.length <= 7) v = (v | 4278190080) >>> 0;
      return toInt322(v);
    }
    throw new NotImplementedComponent(`parse_color: unsupported ${typeof value}`);
  }
  var H_ALIGN = { start: 1, center: 2, end: 3, spacebetween: 6, spaceevenly: 7, spacearound: 8 };
  var V_ALIGN = { start: 1, top: 4, center: 2, bottom: 5, spacebetween: 6, spaceevenly: 7, spacearound: 8 };
  var TEXT_ALIGN = {
    left: 1,
    "1": 1,
    right: 2,
    "2": 2,
    center: 3,
    "3": 3,
    justify: 4,
    "4": 4,
    start: 5,
    "5": 5
  };
  var OVERFLOW = { clip: 1, ellipsis: 3, visible: 2, start_ellipsis: 4, middle_ellipsis: 5 };
  function parseHAlign(a) {
    return H_ALIGN[a.toLowerCase()] ?? 1;
  }
  function parseVAlign(a) {
    return V_ALIGN[a.toLowerCase()] ?? 4;
  }
  function hAlignOf(c, dflt) {
    return parseHAlign(String(c.horizontalAlignment ?? c.horizontalArrangement ?? dflt));
  }
  function vAlignOf(c, dflt) {
    return parseVAlign(String(c.verticalAlignment ?? c.verticalArrangement ?? dflt));
  }
  function parseTextAlign(a) {
    return TEXT_ALIGN[a.toLowerCase()] ?? 5;
  }
  function normalizeComponent(c) {
    if (!("type" in c) && Object.keys(c).length === 1) {
      const key = Object.keys(c)[0];
      const val = c[key];
      const out = { type: key };
      if (Array.isArray(val)) out.children = val;
      else if (val && typeof val === "object") Object.assign(out, val);
      return out;
    }
    return c;
  }
  var INT_MAX2 = 2147483647;
  var NAN_BITS = 2143289344;
  var FIRST_PASS_TYPES = /* @__PURE__ */ new Set([
    "resources",
    "variable",
    "global",
    "definepattern",
    "referencedoperations"
  ]);
  var PATH_MOVE = 10;
  var PATH_LINE = 11;
  var PATH_QUAD = 12;
  var PATH_CUBIC = 14;
  var PATH_CLOSE = 15;
  var SVG_SPLIT = /(?=[MmZzLlHhVvCcSsQqTtAa])/;
  var SVG_SEP = /[,\s]+/;
  var B64_ALPHABET = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
  function decodeBase64(input) {
    let str = input.trim();
    const comma = str.startsWith("data:") ? str.indexOf(",") : -1;
    if (comma >= 0) str = str.slice(comma + 1);
    str = str.replace(/[\s]/g, "");
    str = str.replace(/-/g, "+").replace(/_/g, "/");
    let end = str.length;
    while (end > 0 && str[end - 1] === "=") end--;
    const out = new Uint8Array(Math.floor(end * 3 / 4));
    let acc = 0, bits = 0, o = 0;
    for (let i = 0; i < end; i++) {
      const v = B64_ALPHABET.indexOf(str[i]);
      if (v < 0) throw new NotImplementedComponent(`bad base64 character '${str[i]}'`);
      acc = acc << 6 | v;
      bits += 6;
      if (bits >= 8) {
        bits -= 8;
        out[o++] = acc >> bits & 255;
      }
    }
    return out.subarray(0, o);
  }
  function pngSize(data) {
    const SIG = [137, 80, 78, 71, 13, 10, 26, 10];
    const ihdrOk = data.length >= 24 && SIG.every((b, i) => data[i] === b) && data[12] === 73 && data[13] === 72 && data[14] === 68 && data[15] === 82;
    if (!ihdrOk) {
      throw new NotImplementedComponent(
        "bitmap: only PNG is supported, and this does not start with a PNG header"
      );
    }
    const be = (o) => (data[o] << 24 | data[o + 1] << 16 | data[o + 2] << 8 | data[o + 3]) >>> 0;
    return [be(16), be(20)];
  }
  function arcToCubics(x0, y0, rx, ry, angle, largeArc, sweep, x1, y1) {
    if (rx === 0 || ry === 0) return [];
    const alpha = angle * Math.PI / 180;
    const cosA = Math.cos(alpha), sinA = Math.sin(alpha);
    const dx = (x0 - x1) / 2, dy = (y0 - y1) / 2;
    const x1p = cosA * dx + sinA * dy;
    const y1p = -sinA * dx + cosA * dy;
    let rxp = Math.abs(rx), ryp = Math.abs(ry);
    const check = x1p * x1p / (rxp * rxp) + y1p * y1p / (ryp * ryp);
    if (check > 1) {
      const sc = Math.sqrt(check);
      rxp *= sc;
      ryp *= sc;
    }
    const sign = largeArc === sweep ? -1 : 1;
    const numerator = rxp * rxp * ryp * ryp - rxp * rxp * y1p * y1p - ryp * ryp * x1p * x1p;
    const denominator = rxp * rxp * y1p * y1p + ryp * ryp * x1p * x1p;
    const root = Math.sqrt(Math.max(0, numerator / denominator));
    const cxp = sign * root * rxp * y1p / ryp;
    const cyp = -sign * root * ryp * x1p / rxp;
    const cx = cosA * cxp - sinA * cyp + (x0 + x1) / 2;
    const cy = sinA * cxp + cosA * cyp + (y0 + y1) / 2;
    const theta1 = Math.atan2((y1p - cyp) / ryp, (x1p - cxp) / rxp);
    let dTheta = Math.atan2((-y1p - cyp) / ryp, (-x1p - cxp) / rxp) - theta1;
    if (sweep && dTheta < 0) dTheta += 2 * Math.PI;
    else if (!sweep && dTheta > 0) dTheta -= 2 * Math.PI;
    const segments = Math.ceil(Math.abs(dTheta) / (Math.PI / 2)) || 1;
    const out = [];
    for (let i = 0; i < segments; i++) {
      const s1 = theta1 + i * dTheta / segments;
      const s2 = theta1 + (i + 1) * dTheta / segments;
      const t = 4 / 3 * Math.tan((s2 - s1) / 4);
      const xstart = cosA * rxp * Math.cos(s1) - sinA * ryp * Math.sin(s1) + cx;
      const ystart = sinA * rxp * Math.cos(s1) + cosA * ryp * Math.sin(s1) + cy;
      const xend = cosA * rxp * Math.cos(s2) - sinA * ryp * Math.sin(s2) + cx;
      const yend = sinA * rxp * Math.cos(s2) + cosA * ryp * Math.sin(s2) + cy;
      const cp1x = xstart + t * (-cosA * rxp * Math.sin(s1) - sinA * ryp * Math.cos(s1));
      const cp1y = ystart + t * (-sinA * rxp * Math.sin(s1) + cosA * ryp * Math.cos(s1));
      const cp2x = xend - t * (-cosA * rxp * Math.sin(s2) - sinA * ryp * Math.cos(s2));
      const cp2y = yend - t * (-sinA * rxp * Math.sin(s2) + cosA * ryp * Math.cos(s2));
      out.push([
        Math.fround(cp1x),
        Math.fround(cp1y),
        Math.fround(cp2x),
        Math.fround(cp2y),
        Math.fround(xend),
        Math.fround(yend)
      ]);
    }
    return out;
  }
  function parseSvgPath(pathData) {
    const out = [];
    const cords = [0, 0, 0, 0, 0, 0];
    let cx = 0, cy = 0, sx = 0, sy = 0;
    const verb = (tag) => out.push({ bits: (tag | 4286578688) >>> 0 });
    for (const seg of pathData.split(SVG_SPLIT)) {
      if (!seg || !/[A-Za-z]/.test(seg[0])) continue;
      const cmd = seg[0];
      const rest = seg.slice(1).trim();
      const values = rest ? rest.split(SVG_SEP).filter(Boolean).map(Number) : [];
      if (cmd === "M") {
        if (values.length < 2) throw new NotImplementedComponent(`SVG 'M' needs 2 values`);
        verb(PATH_MOVE);
        out.push(values[0], values[1]);
        cx = values[0];
        cy = values[1];
        sx = cx;
        sy = cy;
      } else if (cmd === "L") {
        for (let i = 0; i + 1 < values.length; i += 2) {
          verb(PATH_LINE);
          out.push(cx, cy, values[i], values[i + 1]);
          cx = values[i];
          cy = values[i + 1];
        }
      } else if (cmd === "H") {
        for (const v of values) {
          verb(PATH_LINE);
          out.push(cx, cy, v, cords[1]);
          cx = v;
          cy = cords[1];
        }
      } else if (cmd === "V") {
        for (const v of values) {
          verb(PATH_LINE);
          out.push(cx, cy, cords[0], v);
          cx = cords[0];
          cy = v;
        }
      } else if (cmd === "C") {
        for (let i = 0; i + 5 < values.length; i += 6) {
          verb(PATH_CUBIC);
          out.push(
            cx,
            cy,
            values[i],
            values[i + 1],
            values[i + 2],
            values[i + 3],
            values[i + 4],
            values[i + 5]
          );
          cx = values[i + 4];
          cy = values[i + 5];
        }
      } else if (cmd === "Q") {
        for (let i = 0; i + 3 < values.length; i += 4) {
          verb(PATH_QUAD);
          out.push(cx, cy, values[i], values[i + 1], values[i + 2], values[i + 3]);
          cx = values[i + 2];
          cy = values[i + 3];
        }
      } else if (cmd === "S") {
        for (let i = 0; i + 3 < values.length; i += 4) {
          verb(PATH_CUBIC);
          out.push(
            cx,
            cy,
            2 * cords[0] - cords[2],
            2 * cords[1] - cords[3],
            values[i],
            values[i + 1],
            values[i + 2],
            values[i + 3]
          );
          cx = values[i + 2];
          cy = values[i + 3];
        }
      } else if (cmd === "A" || cmd === "a") {
        for (let i = 0; i + 6 < values.length; i += 7) {
          const rx = values[i], ry = values[i + 1], ang = values[i + 2];
          const largeArc = values[i + 3] !== 0;
          const swp = values[i + 4] !== 0;
          let ex = values[i + 5], ey = values[i + 6];
          if (cmd === "a") {
            ex += cx;
            ey += cy;
          }
          const segs = arcToCubics(cx, cy, rx, ry, ang, largeArc, swp, ex, ey);
          if (!segs.length) {
            verb(PATH_LINE);
            out.push(cx, cy, ex, ey);
            cx = ex;
            cy = ey;
            continue;
          }
          for (const [c1x, c1y, c2x, c2y, xe, ye] of segs) {
            verb(PATH_CUBIC);
            out.push(cx, cy, c1x, c1y, c2x, c2y, xe, ye);
            cx = xe;
            cy = ye;
          }
        }
      } else if (cmd === "Z" || cmd === "z") {
        verb(PATH_CLOSE);
        cx = sx;
        cy = sy;
      } else {
        throw new NotImplementedComponent(
          `SVG path command '${cmd}' is not supported by the platform parser (only M L H V C Q S A Z)`
        );
      }
      if (cmd !== "Z" && cmd !== "H" && values.length >= 2) {
        cords[0] = values[values.length - 2];
        cords[1] = values[values.length - 1];
        if ((cmd === "C" || cmd === "S") && values.length >= 4) {
          cords[2] = values[values.length - 4];
          cords[3] = values[values.length - 3];
        }
      }
    }
    return out;
  }
  function* eachResource(section) {
    const dictCfg = (v) => v && typeof v === "object" && !Array.isArray(v) ? v : null;
    if (Array.isArray(section)) {
      for (const entry of section) {
        const keys = Object.keys(entry);
        if (!("name" in entry) && keys.length === 1) {
          const name = keys[0];
          yield [name, dictCfg(entry[name]), entry[name]];
        } else {
          yield [String(entry.name), entry, entry.value];
        }
      }
      return;
    }
    for (const [name, value] of Object.entries(section)) {
      yield [name, dictCfg(value), value];
    }
  }
  var PB = {
    TEXT_SIZE: 1,
    COLOR: 4,
    STROKE_WIDTH: 5,
    STROKE_CAP: 7,
    STYLE: 8,
    SHADER: 9,
    ALPHA: 12,
    STROKE_JOIN: 15,
    COLOR_ID: 19
  };
  var STYLE = { fill: 0, stroke: 1, fillandstroke: 2 };
  function numBits(value) {
    const n = Number(value);
    if (Number.isNaN(n) && String(value).trim().toLowerCase() !== "nan") {
      throw new NotImplementedComponent(
        `expected a number, got ${JSON.stringify(value)} (this field takes no expression)`
      );
    }
    return floatToRawIntBits(n);
  }
  var PROJECTION = { perspective: 0, ortho: 1, orthographic: 1 };
  var MATRIX_SUB = { identity: 0, translate: 1, scale: 2, rotate: 3, multiply: 4 };
  var LIGHT_TYPE = { directional: 0, dir: 0, point: 1 };
  var SURFACE = { general: 0, heightfield: 1, sphere: 2, cylinder: 3 };
  var P3_CLEAR_DEPTH = 0;
  var P3_MATERIAL = 1;
  var P3_DEPTH_BIAS = 2;
  var MESH_MODE = {
    "software-flat": 0,
    "software-smooth": 1,
    "canvas-flat": 2,
    "canvas-smooth": 3,
    "drawmesh-flat": 4,
    "drawmesh-smooth": 5,
    "gl-flat": 6,
    "gl-smooth": 7,
    "drawmesh-zbuf-flat": 8,
    "drawmesh-zbuf-smooth": 9,
    "canvas-zbuf-flat": 10,
    "canvas-zbuf-smooth": 11,
    flat: 0,
    smooth: 1
  };
  var MODE_WIREFRAME = 256;
  var WIRE_EDGE = [512, 1024, 2048];
  var PRIMITIVE = {
    sphere: 0,
    cylinder: 1,
    cone: 2,
    cube: 3,
    roundedcube: 4,
    sphericalsector: 5,
    sphericaldome: 6,
    tube: 7,
    captube: 8,
    profiletube: 9,
    torus: 10,
    plane: 11,
    extrudecircle: 12,
    extrudesector: 13,
    extrudesegment: 14,
    extrudearc: 15,
    extruderoundedrect: 16,
    extrudesquircle: 17,
    extrudepath: 18,
    lathe: 19,
    sweep: 20,
    helix: 21,
    icosphere: 22
  };
  var PRIMITIVE_PARAMS = {
    sphere: ["radius", "center"],
    cylinder: ["radius", "from", "to"],
    cone: ["radius", "height", "center"],
    cube: ["dimX", "dimY", "dimZ", "center"],
    roundedcube: ["dimX", "dimY", "dimZ", "center", "cornerRadius"],
    sphericalsector: ["radius", "angle", "center"],
    sphericaldome: ["radius", "angle", "center"],
    torus: ["majorRadius", "minorRadius", "center"],
    plane: ["width", "height", "center"],
    extrudecircle: ["radius", "depth", "center"],
    extrudesector: ["radius", "startAngle", "sweepAngle", "depth", "center"],
    extrudesegment: ["radius", "startAngle", "sweepAngle", "depth", "center"],
    extrudearc: ["innerRadius", "outerRadius", "startAngle", "sweepAngle", "depth", "center"],
    extruderoundedrect: ["width", "height", "cornerRadius", "depth", "center"],
    extrudesquircle: ["radius", "exponent", "depth", "center"],
    icosphere: ["radius", "center"],
    lathe: ["center"],
    helix: ["coilRadius", "tubeRadius", "pitch", "turns", "center"]
  };
  var UV_MODE = { none: 0, uv: 1, uv2: 2, uv3: 3 };
  var FLAG_SPLINE = 1;
  var FLAG_TUBE_LEGACY = 2;
  var FLAG_TUBE_PATH_DENSITY = 4;
  var FLAG_TUBE_CAP = 8;
  var TOUCH_STOP = {
    gently: 0,
    instantly: 1,
    ends: 2,
    notcheseven: 3,
    notchespercents: 4,
    notchesabsolute: 5,
    absolutepos: 6,
    notchessingleeven: 7
  };
  var VECTOR_FUNCTIONS = {
    vec2: 100,
    vec3: 101,
    vec4: 102,
    dot: 103,
    cross: 104,
    length: 105,
    lengthSq: 106,
    normalize: 107
  };
  var VECTOR_COMPONENTS = ["x", "y", "z", "w"];
  var VAR1_BITS = (4286578688 | 3211264 + 70) >>> 0;
  var VAR2_BITS = (4286578688 | 3211264 + 71) >>> 0;
  var PB_GRADIENT = 11;
  var GRAD_LINEAR = 0;
  var GRAD_SWEEP = 2;
  var PAINT_KEYS_ORDER = [
    "shader",
    "color",
    "strokeJoin",
    "strokeCap",
    "style",
    "width",
    "textSize",
    "sweepGradient"
  ];
  var PAINT_KEY_ALIASES = { strokewidth: "width", runtimeshader: "shader" };
  var PAINT_KEYS_IGNORED = ["linearGradient", "pathEffect", "alpha"];
  var CAP = { round: 1, square: 2 };
  var JOIN = { round: 1, bevel: 2 };
  var _Parser = class _Parser {
    constructor(options = {}) {
      /**
       * name -> NaN-boxed id bits, shared with the expression compiler.
       *
       * A float variable declared outside the first pass and without commit/flush is
       * *deferred*: nothing is written where it appears, and its ops are emitted at first
       * use. That placement is not cosmetic — a definition emitted inside a canvas lands in
       * the draw stream, so hoisting or sinking it changes what the document draws.
       */
      this.variables = new DeferredVariables();
      this.colors = /* @__PURE__ */ new Map();
      this.paths = /* @__PURE__ */ new Map();
      this.bitmaps = /* @__PURE__ */ new Map();
      this.profiles = 0;
      this.apiLevel = 7;
      this.inFirstPass = false;
      this.readFile = options.readFile;
    }
    /** Resolve a canvas float field to raw float32 bits (literal, ref or expression). */
    fbits(value) {
      if (value === null || value === void 0) return NAN_BITS;
      if (typeof value === "boolean") return floatToRawIntBits(value ? 1 : 0);
      if (typeof value === "number") return floatToRawIntBits(value);
      if (typeof value === "string") {
        const special = { NaN: 2143289344, Infinity: 2139095040, "-Infinity": 4286578688 };
        if (value in special) return special[value] >>> 0;
        if (value === "max") return floatToRawIntBits(34028234663852886e22);
        if (this.expr.isVariable(value)) return this.expr.variableNanBits(value);
        return this.expr.parseExpression(value);
      }
      if (value && typeof value === "object") return this.expr.parseExpression(value);
      throw new NotImplementedComponent(`float field type ${typeof value}`);
    }
    parse(doc) {
      const header = doc.header ?? {};
      this.profiles = Number(header.profiles ?? 0);
      const apiLevel = Number(header.apiLevel ?? 7);
      if (apiLevel < 7) {
        throw new NotImplementedComponent(
          `apiLevel ${apiLevel}: only apiLevel >= 7 is implemented`
        );
      }
      this.apiLevel = apiLevel;
      const tags = [];
      for (const [key, value] of Object.entries(header)) {
        if (key === "apiLevel" || key === "orderedResources") continue;
        const tag = HEADER_KEY_TO_TAG[key];
        if (tag === void 0) throw new NotImplementedComponent(`Unknown header tag: ${key}`);
        tags.push([tag, value]);
      }
      this.writer = new Writer(apiLevel, tags);
      this.expr = new ExpressionParser(this.writer, this.variables);
      this.variables.resolver = (_name, cmd) => this.materialiseFloatVar(cmd);
      if (doc.resources) {
        this.inFirstPass = true;
        this.parseResources(doc.resources);
        this.inFirstPass = false;
      }
      const root = doc.root;
      if (root === void 0 || root === null) return this.writer.encodeToByteArray();
      const items = (Array.isArray(root) ? root : [root]).map(normalizeComponent);
      this.inFirstPass = true;
      for (const item of items) {
        if (FIRST_PASS_TYPES.has(String(item.type ?? "").toLowerCase())) {
          this.parseComponent(item);
        }
      }
      this.inFirstPass = false;
      this.writer.rootStart();
      for (const item of items) {
        const t = String(item.type ?? "").toLowerCase();
        if (t === "resources" || t === "variable") continue;
        this.parseComponent(item);
      }
      this.writer.containerEnd();
      return this.writer.encodeToByteArray();
    }
    parseModifiers(raw) {
      const out = [];
      for (const mod of raw ?? []) {
        const entries = typeof mod === "string" ? [[mod, "NaN"]] : Object.entries(mod);
        for (const [key, value] of entries) {
          const k = key.toLowerCase();
          if (k === "fillmaxsize") {
            const v = this.fbits(value);
            out.push(modWidth(TYPE_FILL, v), modHeight(TYPE_FILL, v));
          } else if (k === "fillmaxwidth") {
            out.push(modWidth(TYPE_FILL, this.fbits(value)));
          } else if (k === "fillmaxheight") {
            out.push(modHeight(TYPE_FILL, this.fbits(value)));
          } else if (k === "width") {
            out.push(modWidth(TYPE_EXACT, this.fbits(value)));
          } else if (k === "height") {
            out.push(modHeight(TYPE_EXACT, this.fbits(value)));
          } else if (k === "background") {
            if (typeof value !== "string") {
              throw new NotImplementedComponent("background non-string");
            }
            out.push(value.startsWith("$") || value.startsWith("@") ? modBackgroundId(this.resolveColor(value)) : modBackground(parseColor(value)));
          } else if (k === "border") {
            const b = value ?? {};
            out.push(modBorder(
              Number(b.width),
              Number(b.cornerRadius),
              this.resolveColor(b.color),
              Math.trunc(Number(b.shape ?? 0))
            ));
          } else if (k === "verticalscroll" || k === "horizontalscroll") {
            const direction = k === "verticalscroll" ? 0 : 1;
            let position = value;
            if (value && typeof value === "object" && !Array.isArray(value)) {
              const spec = value;
              if (Math.trunc(Number(spec.notches ?? 0)) > 0) {
                throw new NotImplementedComponent("scroll notches TBD");
              }
              position = spec.position ?? 0;
            }
            out.push(modClipRect(), modScroll(direction, this.fbits(position)));
          } else if (k === "visibility") {
            out.push(modVisibility(this.resolveId(value)));
          } else if (k === "clip") {
            const shape = value ?? {};
            const st = String(shape.type ?? "").toLowerCase();
            if (st !== "roundrect" && st !== "roundedrect") {
              throw new NotImplementedComponent(
                `clip shape '${st}' (circle/rect TBD)`
              );
            }
            if ("radius" in shape) {
              const r = Number(shape.radius);
              out.push(modClipRoundedRect(r, r, r, r));
            } else {
              out.push(modClipRoundedRect(
                Number(shape.topStart ?? 0),
                Number(shape.topEnd ?? 0),
                Number(shape.bottomStart ?? 0),
                Number(shape.bottomEnd ?? 0)
              ));
            }
          } else if (k === "size") {
            if (Array.isArray(value)) {
              out.push(
                modWidth(TYPE_EXACT, numBits(value[0])),
                modHeight(TYPE_EXACT, numBits(value[1]))
              );
            } else {
              const v = this.fbits(value);
              out.push(modWidth(TYPE_EXACT, v), modHeight(TYPE_EXACT, v));
            }
          } else if (k === "weight" || k === "horizontalweight") {
            out.push(modWidth(TYPE_WEIGHT, numBits(value)));
          } else if (k === "verticalweight") {
            out.push(modHeight(TYPE_WEIGHT, numBits(value)));
          } else if (k === "widthin") {
            const wi = value;
            out.push(modWidthIn(this.fbits(wi[0]), this.fbits(wi[1])));
          } else if (k === "heightin") {
            const hi = value;
            out.push(modHeightIn(this.fbits(hi[0]), this.fbits(hi[1])));
          } else if (k === "onclick" || k === "multiclick") {
            let raw2 = value, clickType = 0;
            if (k === "multiclick") {
              const mv = value ?? {};
              clickType = Number(mv.clickType ?? 0);
              raw2 = mv.actions;
            }
            out.push(modClick(this.parseActions(raw2), clickType));
          } else if (k === "semantics") {
            const sem = value ?? {};
            const tid = (field) => field in sem ? this.writer.addText(String(sem[field])) : 0;
            out.push(modSemantics(
              tid("contentDescription"),
              -1,
              // role: there is no JSON key for it upstream either
              tid("text"),
              tid("stateDescription"),
              SEMANTICS_MODE_SET,
              sem.enabled === void 0 ? true : Boolean(sem.enabled),
              Boolean(sem.clickable ?? false)
            ));
          } else if (k === "offset") {
            if (Array.isArray(value)) {
              out.push(modOffset(this.fbits(value[0]), this.fbits(value[1])));
            } else if (value && typeof value === "object") {
              const o = value;
              out.push(modOffset(this.fbits(o.x ?? 0), this.fbits(o.y ?? 0)));
            } else {
              throw new NotImplementedComponent(
                `offset modifier value ${JSON.stringify(value)}`
              );
            }
          } else if (k === "padding") {
            const b = this.paddingBits(value);
            out.push(modPadding(b[0], b[1], b[2], b[3]));
          } else {
            throw new NotImplementedComponent(`modifier '${key}'`);
          }
        }
      }
      return out;
    }
    /**
     * Padding, in the reference's three forms.
     *
     * The scalar form goes through the expression path, so `{"padding": "8 + 8"}` is legal;
     * the list and object forms go through getDouble and are numeric only. That asymmetry is
     * the reference's, and matching it matters — accepting an expression in the object form
     * would produce a document the Java converter rejects.
     */
    paddingBits(value) {
      if (Array.isArray(value)) {
        if (value.length < 4) {
          throw new NotImplementedComponent(
            `padding list needs 4 values, got ${value.length}`
          );
        }
        return [numBits(value[0]), numBits(value[1]), numBits(value[2]), numBits(value[3])];
      }
      if (value && typeof value === "object") {
        const g = (k) => numBits(value[k] ?? 0);
        return [g("start"), g("top"), g("end"), g("bottom")];
      }
      const v = this.fbits(value);
      return [v, v, v, v];
    }
    parseComponent(component) {
      const ctype = String(component.type).toLowerCase();
      const mods = this.parseModifiers(component.modifiers);
      const kids = (component.children ?? []).map(normalizeComponent);
      if (ctype === "column" || ctype === "row" || ctype === "box") {
        if (ctype === "box" && kids.length === 0) {
          this.writer.boxLeaf(
            hAlignOf(component, "center"),
            vAlignOf(component, "center"),
            mods
          );
          return;
        }
        const h = hAlignOf(component, "start");
        const v = vAlignOf(component, "top");
        if (ctype === "column") this.writer.startColumn(h, v, mods);
        else if (ctype === "row") this.writer.startRow(h, v, mods);
        else this.writer.startBox(h, v, mods);
        for (const k of kids) this.parseComponent(k);
        this.writer.endContainer();
        return;
      }
      if (ctype === "fitbox") {
        this.writer.startFitBox(
          hAlignOf(component, "center"),
          vAlignOf(component, "center"),
          mods
        );
        for (const k of (component.children ?? []).map(normalizeComponent)) {
          this.parseComponent(k);
        }
        this.writer.endFitBox();
        return;
      }
      if (ctype === "spacer") {
        const m = mods.length ? mods : [modWidth(TYPE_WEIGHT, floatToRawIntBits(1))];
        this.writer.startBox(0, 0, m);
        this.writer.endContainer();
        return;
      }
      if (ctype === "flow") {
        this.writer.startFlow(
          hAlignOf(component, "start"),
          vAlignOf(component, "top"),
          mods,
          Math.trunc(Number(component.maxColumns ?? 2147483647))
        );
        for (const k of (component.children ?? []).map(normalizeComponent)) {
          this.parseComponent(k);
        }
        this.writer.endFlow();
        return;
      }
      if (ctype === "global") {
        const kidsAll = (component.children ?? []).map(normalizeComponent);
        if (this.inFirstPass) {
          this.writer.beginGlobal();
          for (const k of kidsAll) {
            if (FIRST_PASS_TYPES.has(String(k.type).toLowerCase())) this.parseComponent(k);
          }
          this.writer.endGlobal();
        } else {
          for (const k of kidsAll) {
            if (!FIRST_PASS_TYPES.has(String(k.type).toLowerCase())) this.parseComponent(k);
          }
        }
        return;
      }
      if (ctype === "resources") {
        if (this.inFirstPass) this.parseResources(component);
        return;
      }
      if (ctype === "variable") {
        if (this.inFirstPass || !this.variables.isMaterialised(component.name)) {
          this.parseVariable(component);
        }
        return;
      }
      if (ctype === "text") {
        this.parseText(component, mods);
        return;
      }
      if (ctype === "canvas") {
        this.writer.startCanvas(mods, this.apiLevel);
        for (const cmd of component.commands ?? []) this.parseCommand(cmd);
        this.writer.endCanvas(this.apiLevel);
        return;
      }
      throw new NotImplementedComponent(`component type '${ctype}'`);
    }
    /** `{"drawRect": {...}}` normalises the same way a component does. */
    normalizeCommand(c) {
      if (!("type" in c) && Object.keys(c).length === 1) {
        const key = Object.keys(c)[0];
        const val = c[key];
        const out = { type: key };
        if (Array.isArray(val)) out.commands = val;
        else if (val && typeof val === "object") Object.assign(out, val);
        else if (["drawpath", "pathappendclose"].includes(key.toLowerCase())) out.path = val;
        else out.value = val;
        return out;
      }
      return c;
    }
    /** One paint property, appended to the bundle as PaintBundle ints. */
    paintSetter(key, src, ints) {
      if (key === "shader") {
        const v = src[key];
        if (typeof v !== "number") {
          throw new NotImplementedComponent(
            "paint shader: only a numeric shader id is supported; an AGSL runtime shader needs DATA_SHADER, which this converter does not implement"
          );
        }
        ints.push(PB.SHADER, v);
      } else if (key === "color") {
        const c = src.color;
        if (typeof c === "string" && (c.startsWith("$colors.") || c.startsWith("@colors."))) {
          ints.push(PB.COLOR_ID, this.resolveColor(c));
        } else {
          ints.push(PB.COLOR, toInt322(parseColor(c)));
        }
      } else if (key === "style") {
        const v = STYLE[String(src.style).toLowerCase()];
        if (v === void 0) throw new NotImplementedComponent(`paint style ${src.style}`);
        ints.push(PB.STYLE | v << 16);
      } else if (key === "alpha") ints.push(PB.ALPHA, this.fbits(src.alpha));
      else if (key === "width" || key === "strokeWidth") {
        ints.push(
          PB.STROKE_WIDTH,
          this.fbits("strokeWidth" in src ? src.strokeWidth : src.width)
        );
      } else if (key === "strokeCap") {
        ints.push(PB.STROKE_CAP | (CAP[String(src.strokeCap).toLowerCase()] ?? 0) << 16);
      } else if (key === "strokeJoin") {
        ints.push(PB.STROKE_JOIN | (JOIN[String(src.strokeJoin).toLowerCase()] ?? 0) << 16);
      } else if (key === "textSize") ints.push(PB.TEXT_SIZE, this.fbits(src.textSize));
      else if (key === "linearGradient") this.gradient(src.linearGradient, ints, false);
      else if (key === "sweepGradient") this.gradient(src.sweepGradient, ints, true);
      else throw new NotImplementedComponent(`paint key '${key}'`);
    }
    /**
     * A linear or sweep gradient, appended to the paint bundle.
     *
     * The colour array is packed as one int: the count in the low half, and in the high
     * half a bitmask marking which entries are `$colors.` references rather than literals.
     * Stops are optional and their count is written even when zero.
     *
     * A sweep gradient takes a centre and no tile mode; a linear one takes two points and
     * a tile mode. The two are the same op distinguished only by the type in the high half
     * of the leading int, which makes an accidental swap produce a valid-looking bundle.
     */
    gradient(g, ints, sweep) {
      const colorsArr = g.colors ?? [];
      const colors = [];
      let idMask = 0;
      colorsArr.forEach((c, i) => {
        if (typeof c === "string" && (c.startsWith("$colors.") || c.startsWith("@colors."))) {
          idMask |= 1 << i;
          colors.push(toInt322(this.resolveColor(c)));
        } else {
          colors.push(toInt322(typeof c === "string" ? this.resolveColor(c) : parseColor(c)));
        }
      });
      const stops = g.stops;
      ints.push(PB_GRADIENT | (sweep ? GRAD_SWEEP : GRAD_LINEAR) << 16);
      ints.push((idMask << 16 | colors.length) & 4294967295 | 0);
      ints.push(...colors);
      ints.push(stops ? stops.length : 0);
      if (stops) for (const st of stops) ints.push(floatToRawIntBits(Number(st)));
      if (sweep) {
        ints.push(this.fbits(g.centerX), this.fbits(g.centerY));
      } else {
        ints.push(
          this.fbits(g.x1 ?? 0),
          this.fbits(g.y1 ?? 0),
          this.fbits(g.x2 ?? 0),
          this.fbits(g.y2 ?? 0)
        );
        const tm = g.tileMode ?? 0;
        ints.push(typeof tm === "number" && Number.isInteger(tm) ? tm : 0);
      }
    }
    /**
     * A named bitmap, with its bytes embedded in the document.
     *
     * Upstream sources bitmaps from a host-supplied map keyed by name, which a file-based
     * converter has no equivalent of, so the JSON supplies the image itself: `base64` for a
     * self-contained document, or `file` for one that reads from disk. Either way the bytes
     * end up inside the `.rc` — a document that still needs an external file to render is
     * not a document.
     *
     * `file` needs a reader, because the converter core deliberately has no filesystem
     * dependency; the CLI supplies one. Without it, `file` raises rather than silently
     * producing a document with no image in it.
     */
    declareBitmap(name, value) {
      let spec = typeof value === "string" ? { file: value } : value;
      if (!spec || typeof spec !== "object") {
        throw new NotImplementedComponent(`bitmap '${name}': expected an object`);
      }
      let data;
      if ("base64" in spec) {
        try {
          data = decodeBase64(String(spec.base64));
        } catch (e) {
          throw new NotImplementedComponent(
            `bitmap '${name}': bad base64: ${e.message}`
          );
        }
      } else if ("file" in spec) {
        if (!this.readFile) {
          throw new NotImplementedComponent(
            `bitmap '${name}' names a file, but this converter was given no file reader \u2014 pass one via convert(json, {readFile}), or inline the image as base64`
          );
        }
        try {
          data = this.readFile(String(spec.file));
        } catch (e) {
          throw new NotImplementedComponent(
            `bitmap '${name}': ${e.message}`
          );
        }
      } else {
        throw new NotImplementedComponent(`bitmap '${name}' needs 'file' or 'base64'`);
      }
      const [width, height] = pngSize(data);
      const imageId = this.writer.allocDataId();
      this.writer.addBitmap(imageId, width, height, data);
      return imageId;
    }
    /**
     * Mirror RemoteComposeJsonParser.resolveTextId.
     *
     * A text argument is not always a literal string: an integer is already an id, `@name`
     * resolves to whatever that variable's id is, and `#RRGGBB` becomes a colour constant.
     * That is what lets `drawTextAnchored` show a value that changes at runtime rather than
     * a string fixed at build time.
     */
    resolveTextId(obj) {
      if (typeof obj === "boolean") {
        throw new NotImplementedComponent(`text id: unexpected boolean ${obj}`);
      }
      if (typeof obj === "number") {
        if (Number.isInteger(obj)) return obj;
        return idFromNanBits(this.writer.addFloatConstant(obj));
      }
      if (obj && typeof obj === "object") {
        const kind = String(obj.type ?? "").toLowerCase();
        if (kind === "textfromfloat") {
          const vo = obj;
          return this.writer.addTextFromFloat(
            this.fbits(vo.value),
            Number(vo.whole ?? 0),
            Number(vo.decimal ?? 3),
            Number(vo.flags ?? 0)
          );
        }
        throw new NotImplementedComponent(
          kind === "textmerge" ? "textMerge needs a TEXT_MERGE writer, which this converter does not have; no corpus document exercises it" : `text id object '${kind}'`
        );
      }
      const text = String(obj);
      if (text.startsWith("$colors.") || text.startsWith("@colors.")) {
        const hit = this.colors.get(text.slice(8));
        if (hit !== void 0) return hit;
      }
      if (text.includes("@") && /[+\-*/%]/.test(text)) {
        throw new NotImplementedComponent(
          `text id '${text}' is an integer expression, which needs integer-variable support this converter does not have`
        );
      }
      if (/^[@$]/.test(text)) {
        const name = text.slice(1);
        const bits = this.variables.get(name);
        if (bits !== void 0) {
          if ((bits >>> 23 & 255) === 255 && (bits & 8388607) !== 0) {
            return bits & 8388607;
          }
          return Math.trunc(f32FromBits(bits));
        }
      }
      if (text.startsWith("#")) return this.writer.addColor(toInt322(parseColor(text)));
      return this.writer.addText(text);
    }
    /** A bitmap reference: a numeric id, or `@name`/`$name` from `resources.bitmaps`. */
    bitmapId(value) {
      if (typeof value === "string") {
        const name = /^[@$]/.test(value) ? value.slice(1) : value;
        const hit = this.bitmaps.get(name);
        if (hit !== void 0) return hit;
        const known = [...this.bitmaps.keys()].sort().join(", ") || "none declared";
        throw new NotImplementedComponent(
          `bitmap '${value}' not found (declared: ${known})`
        );
      }
      return Math.trunc(Number(value));
    }
    /** `mode` plus the wireframe flags, resolved into the packed mode word. */
    meshMode(command) {
      const raw = command.mode ?? "software-flat";
      let mode;
      if (typeof raw === "number") mode = Math.trunc(raw);
      else {
        const key = String(raw).toLowerCase();
        if (!(key in MESH_MODE)) {
          throw new NotImplementedComponent(
            `drawMesh3D mode '${raw}' (expected one of ${Object.keys(MESH_MODE).sort().join(", ")})`
          );
        }
        mode = MESH_MODE[key];
      }
      if (command.wireframe) {
        mode |= MODE_WIREFRAME;
        for (const e of command.edges ?? []) {
          const n = Math.trunc(Number(e));
          if (!(n >= 0 && n <= 2)) {
            throw new NotImplementedComponent(`drawMesh3D edge ${e} (expected 0, 1 or 2)`);
          }
          mode |= WIRE_EDGE[n];
        }
      }
      return mode;
    }
    /** Flatten a list of dim-component points, rejecting a wrong arity loudly. */
    points(command, key, dim) {
      const pts = command[key];
      if (!pts || !pts.length) {
        throw new NotImplementedComponent(`meshPrimitive3D: missing '${key}'`);
      }
      const out = [];
      for (const pt of pts) {
        if (!Array.isArray(pt) || pt.length !== dim) {
          throw new NotImplementedComponent(
            `meshPrimitive3D: ${key} entries need ${dim} components, got ${Array.isArray(pt) ? pt.length : 0}`
          );
        }
        out.push(...pt.map(Number));
      }
      return out;
    }
    /**
     * Mirror RemoteComposeJsonParser.resolveTextId.
     *
     * An **integer is already an id** and passes through untouched — `{"visibility": 2}`
     * means the INVISIBLE constant, not a reference to variable 2. A float becomes a float
     * constant and yields that constant's id; anything else resolves as a reference.
     *
     * Port note: Python's json distinguishes `2` from `2.0` and this branches on that.
     * `JSON.parse` does not — both arrive as the number 2 — so an id literally written
     * `2.0` is treated here as the id 2 where rcj would make it a float constant. No corpus
     * document does that (every action target is a string reference), but it is a real
     * divergence rather than a resolved one.
     */
    resolveId(value) {
      if (typeof value === "boolean") {
        throw new NotImplementedComponent(`expected an id, got boolean ${value}`);
      }
      if (typeof value === "number") {
        if (Number.isInteger(value)) return value;
        return idFromNanBits(this.writer.addFloatConstant(value));
      }
      return idFromNanBits(this.fbits(value));
    }
    /**
     * Actions for a click modifier. Mirrors DefaultModifierParsers.parseActions: a single
     * object is accepted as well as an array.
     *
     * The eager/lazy split below is load-bearing. Host actions and literal string values
     * allocate their text ids inside the emit thunk, because upstream `write()` calls
     * `addText` itself and the DATA_TEXT therefore lands *after* the container op.
     * Expressions are compiled here, during parsing, because upstream `parseFloat` runs
     * while parsing and the expression op lands *before* it. Swapping either one shifts
     * every id that follows and changes the whole tail of the document.
     */
    parseActions(raw) {
      if (raw === null || raw === void 0) return [];
      const items = Array.isArray(raw) ? raw : [raw];
      const actions = [];
      for (const item of items) {
        if (!item || typeof item !== "object" || Array.isArray(item)) {
          throw new NotImplementedComponent(
            `action ${JSON.stringify(item)} (expected an object)`
          );
        }
        const a = item;
        const t = String(a.type ?? "").toLowerCase();
        const target = a.targetId !== void 0 ? a.targetId : a.target;
        const isHost = t === "hostaction" || t === "hostnamedaction" || t === "hostmetadataaction";
        if (isHost && !("name" in a)) {
          if (!("actionId" in a)) {
            throw new NotImplementedComponent(`${t} needs a "name" or an "actionId"`);
          }
          const actionId = Number(a.actionId);
          if ("metadata" in a) {
            const metadata = String(a.metadata);
            actions.push((w2) => hostAction(actionId, w2.addText(metadata))(w2));
          } else {
            actions.push(hostAction(
              actionId,
              "metadataId" in a ? Number(a.metadataId) : -1
            ));
          }
        } else if (isHost) {
          const name = String(a.name);
          const value = "value" in a ? String(a.value) : null;
          const type = value !== null ? Number(a.actionType ?? HOST_ACTION_STRING_TYPE) : -1;
          const valueId = value !== null ? this.writer.addText(value) : -1;
          actions.push((w2) => {
            const textId = w2.addText(name);
            hostNamedAction(textId, type, valueId)(w2);
          });
        } else if (t === "valuefloatexpressionchange") {
          if (target === void 0 || target === null) {
            throw new NotImplementedComponent(
              'valueFloatExpressionChange needs a "target"'
            );
          }
          const targetId = this.resolveId(target);
          const rawValue = "value" in a ? a.value : a.expression;
          if (rawValue === void 0 || rawValue === null) {
            throw new NotImplementedComponent(
              'valueFloatExpressionChange needs a "value" or "expression"'
            );
          }
          actions.push(valueFloatExpressionChange(
            targetId,
            idFromNanBits(this.fbits(rawValue))
          ));
        } else if (t === "valuefloatchange") {
          if (target === void 0 || target === null) {
            throw new NotImplementedComponent('valueFloatChange needs a "target"');
          }
          actions.push(valueFloatChange(this.resolveId(target), this.fbits(a.value)));
        } else if (t === "valueintegerchange") {
          if (target === void 0 || target === null) {
            throw new NotImplementedComponent('valueIntegerChange needs a "target"');
          }
          actions.push(valueIntegerChange(this.resolveId(target), Number(a.value)));
        } else if (t === "valuestringchange") {
          if (target === void 0 || target === null) {
            throw new NotImplementedComponent('valueStringChange needs a "target"');
          }
          const targetId = this.resolveId(target);
          const value = a.value;
          if (typeof value === "string" && !value.startsWith("@") && !value.startsWith("$")) {
            actions.push((w) => valueStringChange(targetId, w.addText(value))(w));
          } else {
            actions.push(valueStringChange(targetId, this.resolveId(value)));
          }
        } else if (t === "valueintegerexpressionchange") {
          const value = "value" in a ? a.value : a.expression;
          if (typeof target !== "number" || typeof value !== "number") {
            throw new NotImplementedComponent(
              "valueIntegerExpressionChange with a named target or an expression string needs integer-variable support, which this converter does not have; only literal ids are accepted"
            );
          }
          actions.push(valueIntegerExpressionChange(Math.trunc(target), Math.trunc(value)));
        } else {
          throw new NotImplementedComponent(`click action type '${t}'`);
        }
      }
      return actions;
    }
    /**
     * A path reference: a name defined by pathCreate, a `$paths.` lookup, or an SVG string.
     *
     * An SVG string is *not* cached by value: the reference parses to a fresh float array
     * each call and keys its cache on that array, so two identical strings each get their
     * own id and their own DATA_PATH op.
     */
    resolvePath(value) {
      const s = String(value);
      const hit = this.paths.get(s);
      if (hit !== void 0) return hit;
      if (s.startsWith("$paths.") || s.startsWith("@paths.")) {
        const name = s.slice(7);
        const id = this.paths.get(name);
        if (id === void 0) throw new NotImplementedComponent(`Path not found: ${name}`);
        return id;
      }
      return this.writer.addPathData(parseSvgPath(s));
    }
    /** A colour value: literal, "$colors.name", or a variable holding a colour id. */
    resolveColor(value) {
      if (typeof value === "string" && (value.startsWith("$colors.") || value.startsWith("@colors."))) {
        const name = value.slice(8);
        const id = this.colors.get(name);
        if (id === void 0) throw new NotImplementedComponent(`Color not found: ${name}`);
        return id;
      }
      return toInt322(parseColor(value));
    }
    buildPaintBundle(command) {
      const ints = [];
      if ("ops" in command) {
        for (const op of command.ops) {
          for (const k of Object.keys(op)) {
            this.paintSetter(PAINT_KEY_ALIASES[k.toLowerCase()] ?? k, op, ints);
          }
        }
        return ints;
      }
      for (const k of PAINT_KEYS_IGNORED) {
        if (k in command) {
          throw new NotImplementedComponent(
            `paint key '${k}' is ignored as a direct key by the reference parser \u2014 move it into an "ops" array`
          );
        }
      }
      const present = new Set(Object.keys(command).map((k) => PAINT_KEY_ALIASES[k.toLowerCase()] ?? k));
      for (const key of PAINT_KEYS_ORDER) {
        if (present.has(key)) this.paintSetter(key, command, ints);
      }
      return ints;
    }
    parseCommand(raw) {
      const command = this.normalizeCommand(raw);
      const t = String(command.type).toLowerCase();
      const w = this.writer;
      const f = (k) => {
        if (!(k in command)) {
          throw new NotImplementedComponent(`${command.type}: missing required field '${k}'`);
        }
        return this.fbits(command[k]);
      };
      if (t === "variable") {
        this.parseVariable(command);
        return;
      }
      if (t === "paint") {
        w.paintValues(this.buildPaintBundle(command));
        return;
      }
      if (t === "setcolor") {
        w.paintValues([PB.COLOR, toInt322(parseColor(command.color))]);
        return;
      }
      if (t === "setstyle") {
        const v = STYLE[String(command.style).toLowerCase()];
        if (v === void 0) throw new NotImplementedComponent(`setStyle ${command.style}`);
        w.paintValues([PB.STYLE | v << 16]);
        return;
      }
      if (t === "setstrokewidth") {
        w.paintValues([PB.STROKE_WIDTH, f("width")]);
        return;
      }
      if (t === "drawrect") {
        w.drawRect(f("left"), f("top"), f("right"), f("bottom"));
        return;
      }
      if (t === "drawoval") {
        w.drawOval(f("left"), f("top"), f("right"), f("bottom"));
        return;
      }
      if (t === "drawcircle") {
        w.drawCircle(f("cx"), f("cy"), f("radius"));
        return;
      }
      if (t === "drawline") {
        w.drawLine(f("x1"), f("y1"), f("x2"), f("y2"));
        return;
      }
      if (t === "drawroundrect") {
        w.drawRoundRect(f("left"), f("top"), f("right"), f("bottom"), f("rx"), f("ry"));
        return;
      }
      if (t === "drawarc") {
        w.drawArc(f("left"), f("top"), f("right"), f("bottom"), f("startAngle"), f("sweepAngle"));
        return;
      }
      if (t === "drawsector") {
        w.drawSector(f("left"), f("top"), f("right"), f("bottom"), f("startAngle"), f("sweepAngle"));
        return;
      }
      if (t === "drawtextanchored") {
        w.drawTextAnchored(
          this.resolveTextId(command.text),
          f("x"),
          f("y"),
          f("panX"),
          f("panY"),
          Number(command.flags ?? 0)
        );
        return;
      }
      if (t === "save") {
        w.matrixSave();
        if ("commands" in command) {
          for (const c of command.commands) this.parseCommand(c);
          w.matrixRestore();
        }
        return;
      }
      if (t === "restore") {
        w.matrixRestore();
        return;
      }
      if (t === "translate") {
        w.matrixTranslate(f("dx"), f("dy"));
        return;
      }
      if (t === "scale") {
        w.matrixScale(f("sx"), f("sy"));
        return;
      }
      if (t === "rotate") {
        const hasPivot = "pivotX" in command || "centerX" in command;
        const px = hasPivot ? this.fbits(command.pivotX ?? command.centerX) : NAN_BITS;
        const py = hasPivot ? this.fbits(command.pivotY ?? command.centerY) : NAN_BITS;
        w.matrixRotate(f("angle"), px, py);
        return;
      }
      if (t === "drawpath") {
        w.drawPath(this.resolvePath(command.path));
        return;
      }
      if (t === "clippath") {
        w.clipPath(this.resolvePath(command.path));
        return;
      }
      if (t === "drawtextonpath") {
        const off = (k) => k in command ? this.fbits(command[k]) : floatToRawIntBits(0);
        w.drawTextOnPath(
          this.resolveTextId(command.text),
          this.resolvePath(command.path),
          off("hOffset"),
          off("vOffset")
        );
        return;
      }
      if (t === "adddatalist" || t === "idlist" || t === "datalistids") {
        const ids = command.list.map((v) => this.resolveTextId(v));
        const listId = w.addList(ids);
        if (command.id != null) {
          this.variables.set(String(command.id), floatToRawIntBits(listId));
        }
        return;
      }
      if (t === "textlookup") {
        const arr = command.array;
        const arrId = typeof arr === "string" && arr.startsWith("@") ? Math.trunc(f32FromBits(this.variables.get(arr.slice(1)) ?? 0)) : Number(arr);
        const idxBits = "index" in command ? this.fbits(command.index) : floatToRawIntBits(0);
        const textId = w.textLookup(arrId, idxBits);
        if (command.varName != null) {
          this.variables.set(String(command.varName), floatToRawIntBits(textId));
        }
        return;
      }
      if (t === "rem") {
        const m = command.text ?? command.value ?? command.message ?? command.comment ?? "";
        w.rem(String(m));
        return;
      }
      if (t === "pathcreate") {
        const pid = w.pathCreate(f("x"), f("y"));
        if (command.id !== void 0 && command.id !== null) {
          this.paths.set(String(command.id), pid);
        }
        return;
      }
      if (t === "pathexpression") {
        const xOps = this.expr.infixToRpn(String(command.expressionX));
        const yOps = this.expr.infixToRpn(String(command.expressionY));
        const pid = w.pathExpression(
          Number(command.flags ?? 0),
          f("start"),
          f("end"),
          f("count"),
          xOps,
          yOps
        );
        if (command.id !== void 0 && command.id !== null) {
          this.paths.set(String(command.id), pid);
        }
        return;
      }
      if (t === "pathappendlineto") {
        w.pathAppendLineTo(this.resolvePath(command.path), f("x"), f("y"));
        return;
      }
      if (t === "pathappendclose") {
        w.pathAppendClose(this.resolvePath(command.path));
        return;
      }
      if (t === "createparticles") {
        const names = command.variables.map(String);
        const inits = command.initialValues.map((v) => this.expr.infixToRpn(String(v)));
        if (inits.length !== names.length) {
          throw new NotImplementedComponent(
            `createParticles: ${names.length} variables but ${inits.length} initialValues`
          );
        }
        const [systemId, varIds] = w.createParticles(inits, Math.trunc(Number(command.count)));
        this.variables.set(String(command.id), asNanBits(systemId));
        names.forEach((n, i) => this.variables.set(n, asNanBits(varIds[i])));
        return;
      }
      if (t === "particlesloop") {
        const systemBits = f("system");
        const restart = "restart" in command ? this.expr.infixToRpn(String(command.restart)) : null;
        const eqs = command.equations.map((e) => this.expr.infixToRpn(String(e)));
        w.particlesLoopStart(systemBits & 8388607, restart, eqs);
        for (const c of command.commands ?? []) this.parseCommand(c);
        w.containerEnd();
        return;
      }
      if (t === "particlescomparison") {
        const eqs = (key) => {
          const raw2 = command[key];
          return raw2 !== void 0 && raw2 !== null ? raw2.map((e) => this.expr.infixToRpn(String(e))) : null;
        };
        const cond = "condition" in command ? this.expr.infixToRpn(String(command.condition)) : null;
        const then2 = "then2" in command ? eqs("then2") : eqs("then");
        w.particlesCompareStart(
          f("systemId") & 8388607,
          Math.trunc(Number(command.flags ?? 0)),
          f("min"),
          f("max"),
          cond,
          eqs("then1"),
          then2
        );
        for (const c of command.commands ?? []) this.parseCommand(c);
        w.containerEnd();
        return;
      }
      if (t === "impulse") {
        w.impulseStart(f("duration"), f("start"));
        for (const c of command.commands ?? []) this.parseCommand(c);
        w.containerEnd();
        return;
      }
      if (t === "impulseprocess") {
        w.impulseProcessStart();
        for (const c of command.commands ?? []) this.parseCommand(c);
        w.containerEnd();
        return;
      }
      if (t === "cleardepth3d") {
        w.paint3DState(P3_CLEAR_DEPTH, []);
        return;
      }
      if (t === "material3d") {
        w.paint3DState(P3_MATERIAL, [
          this.fbits(command.specular ?? 0),
          this.fbits(command.shininess ?? 32)
        ]);
        return;
      }
      if (t === "depthbias3d") {
        w.paint3DState(P3_DEPTH_BIAS, [
          this.fbits(command.constant ?? 0),
          this.fbits(command.slope ?? 0)
        ]);
        return;
      }
      if (t === "drawmesh3d") {
        w.drawMesh3D(Number(command.mesh), this.meshMode(command));
        return;
      }
      if (t === "addmesh2d") {
        const verts = command.verts.map((v) => Number(v));
        const colors = (command.colors ?? []).map((c) =>
          typeof c === "string" ? toInt322(parseColor(c)) : (Number(c) | 0));
        const uv = (command.uv ?? []).map((v) => Number(v));
        const indices = command.indices.map((i) => Math.trunc(Number(i)));
        if (indices.length % 3) {
          throw new NotImplementedComponent(
            `addMesh2D indices length ${indices.length} is not a multiple of 3`);
        }
        if (verts.length % 2) {
          throw new NotImplementedComponent(
            `addMesh2D verts length ${verts.length} is not a multiple of 2`);
        }
        if (colors.length && colors.length !== verts.length / 2) {
          throw new NotImplementedComponent(
            `addMesh2D colors ${colors.length} != one per vertex (${verts.length / 2})`);
        }
        w.addMesh2DValues(Number(command.id), verts, colors, indices, uv);
        return;
      }
      if (t === "drawmesh2d") {
        w.drawMesh2D(Number(command.mesh ?? command.id),
                     Math.trunc(Number(command.blend ?? 0)),
                     Math.trunc(Number(command.image ?? 0)));
        return;
      }
      if (t === "texture3d") {
        const bitmap = command.bitmap ?? 0;
        w.setTexture3D(bitmap === 0 || bitmap === null ? 0 : this.bitmapId(bitmap));
        return;
      }
      if (t === "camera3d") {
        const projName = String(command.projection ?? "perspective").toLowerCase();
        if (!(projName in PROJECTION)) {
          throw new NotImplementedComponent(`camera3D projection '${projName}'`);
        }
        const proj = PROJECTION[projName];
        const projParams = proj === 0 ? [
          this.fbits(command.fovY ?? 0.9),
          this.fbits(command.aspect ?? 1),
          this.fbits(command.near ?? 0.1),
          this.fbits(command.far ?? 100)
        ] : [
          f("left"),
          f("right"),
          f("bottom"),
          f("top"),
          this.fbits(command.near ?? 0.1),
          this.fbits(command.far ?? 100)
        ];
        const eye = command.eye ?? [0, 0, 4];
        const center = command.center ?? [0, 0, 0];
        const up = command.up ?? [0, 1, 0];
        const view = [...eye, ...center, ...up].map((v) => this.fbits(v));
        if (view.length !== 9) {
          throw new NotImplementedComponent(
            "camera3D needs eye/center/up of 3 components each"
          );
        }
        w.setCamera3D(proj, projParams, view);
        return;
      }
      if (t === "matrix3d") {
        if (!("op" in command)) {
          const extra = Object.keys(command).filter((k) => k !== "type").sort();
          if (extra.length) {
            throw new NotImplementedComponent(
              `matrix3D needs an "op" (identity/translate/scale/rotate/multiply); got keys ${JSON.stringify(extra)}`
            );
          }
        }
        const subName = String(command.op ?? "identity").toLowerCase();
        if (!(subName in MATRIX_SUB)) {
          throw new NotImplementedComponent(`matrix3D op '${subName}'`);
        }
        const sub = MATRIX_SUB[subName];
        let args;
        if (sub === 0) args = [];
        else if (sub === 1 || sub === 2) {
          args = [
            this.fbits(command.x ?? 0),
            this.fbits(command.y ?? 0),
            this.fbits(command.z ?? 0)
          ];
        } else if (sub === 3) {
          const axis = command.axis ?? [0, 1, 0];
          args = [f("angle"), ...axis.map((v) => this.fbits(v))];
          if (args.length !== 4) {
            throw new NotImplementedComponent("matrix3D rotate axis needs 3 components");
          }
        } else {
          const m = command.m;
          if (!m || m.length !== 16) {
            throw new NotImplementedComponent(
              `matrix3D multiply needs 16 floats, got ${m ? m.length : 0}`
            );
          }
          args = m.map((v) => this.fbits(v));
        }
        w.matrix3DOp(sub, args);
        return;
      }
      if (t === "lights3d") {
        const types = [], colors = [], params = [];
        for (const light of command.lights ?? []) {
          const tname = String(light.type ?? "directional").toLowerCase();
          if (!(tname in LIGHT_TYPE)) {
            throw new NotImplementedComponent(`lights3D type '${tname}'`);
          }
          const ltype = LIGHT_TYPE[tname];
          types.push(ltype);
          colors.push(toInt322(parseColor(light.color ?? "#FFFFFFFF")));
          const vec = ltype === 1 ? light.pos : light.dir;
          if (!vec || vec.length !== 3) {
            throw new NotImplementedComponent(
              `lights3D ${tname} needs a 3-component ${ltype === 1 ? "pos" : "dir"}`
            );
          }
          params.push(
            ...vec.map((v) => this.fbits(v)),
            this.fbits(light.intensity ?? 1)
          );
        }
        w.setLights3D(types, colors, params);
        return;
      }
      if (t === "definemesh3d") {
        const verts = command.verts.map((v) => this.fbits(v));
        const normalsRaw = command.normals ?? [];
        const uvRaw = command.uv ?? [];
        const normals = normalsRaw.length ? normalsRaw.map((v) => this.fbits(v)) : null;
        const uv = uvRaw.length ? uvRaw.map((v) => this.fbits(v)) : null;
        const idx = command.indices.map((i) => Math.trunc(Number(i)));
        if (idx.length % 3) {
          throw new NotImplementedComponent(
            `defineMesh3D indices length ${idx.length} is not a multiple of 3`
          );
        }
        if (verts.length % 3) {
          throw new NotImplementedComponent(
            `defineMesh3D verts length ${verts.length} is not a multiple of 3`
          );
        }
        if (normals && normals.length !== verts.length) {
          throw new NotImplementedComponent(
            `defineMesh3D normals ${normals.length} != verts ${verts.length}`
          );
        }
        if (uv && uv.length !== verts.length / 3 * 2) {
          throw new NotImplementedComponent(
            `defineMesh3D uv ${uv.length} != 2 per vertex (${verts.length / 3 * 2})`
          );
        }
        w.defineMesh3D(Number(command.id), idx, verts, normals, uv);
        return;
      }
      if (t === "vectorexpression") {
        const dim = Math.trunc(Number(command.dimension ?? 3));
        if (![2, 3, 4].includes(dim)) {
          throw new NotImplementedComponent(
            `vectorExpression dimension ${dim} (expected 2, 3 or 4)`
          );
        }
        const ops = this.expr.infixToRpn(String(command.expression), VECTOR_FUNCTIONS);
        const base = w.vectorExpression(dim, Math.trunc(Number(command.flags ?? 0)), ops);
        if (command.name) {
          for (let k = 0; k < dim; k++) {
            this.variables.set(
              `${command.name}.${VECTOR_COMPONENTS[k]}`,
              asNanBits(base + k)
            );
          }
        }
        return;
      }
      if (t === "touchexpression") {
        let mode = command.stopMode !== void 0 ? command.stopMode : command.touchMode !== void 0 ? command.touchMode : 0;
        if (typeof mode === "string") {
          const key = mode.toLowerCase().replace(/_/g, "");
          if (!(key in TOUCH_STOP)) {
            throw new NotImplementedComponent(
              `touchExpression stopMode '${mode}' (expected one of ${Object.keys(TOUCH_STOP).sort().join(", ")})`
            );
          }
          mode = TOUCH_STOP[key];
        }
        const minB = "min" in command ? f("min") : NAN_BITS;
        const velB = "velocityId" in command ? f("velocityId") : NAN_BITS;
        const rawExpr = command.expression ?? "touchX()";
        const ops = Array.isArray(rawExpr) ? rawExpr.map((v) => this.fbits(v)) : this.expr.infixToRpn(String(rawExpr));
        const bits = w.touchExpression(
          this.fbits(command.defaultValue ?? 0),
          minB,
          this.fbits(command.max ?? 1),
          Math.trunc(Number(mode)),
          velB,
          Math.trunc(Number(command.touchEffects ?? 0)),
          ops,
          command.touchSpec ? command.touchSpec.map(Number) : null,
          command.easingSpec ? command.easingSpec.map(Number) : null
        );
        if (command.name) this.variables.set(String(command.name), bits);
        return;
      }
      if (t === "meshprimitive3d") {
        const kind = String(command.primitive).toLowerCase().replace(/_/g, "").replace(/-/g, "");
        if (!(kind in PRIMITIVE)) {
          throw new NotImplementedComponent(
            `meshPrimitive3D primitive '${command.primitive}' (expected one of ${Object.keys(PRIMITIVE).sort().join(", ")})`
          );
        }
        const ptype = PRIMITIVE[kind];
        let flagsExtra = 0;
        const extraChannels = [];
        let scalars;
        const fbAll = (xs) => xs.map((v) => this.fbits(v));
        if ("params" in command) {
          scalars = fbAll(command.params);
        } else if (kind === "tube" || kind === "captube") {
          scalars = [f("radius")];
          if ("ringsPerSpan" in command) {
            flagsExtra |= FLAG_TUBE_PATH_DENSITY;
            scalars.push(f("ringsPerSpan"));
          }
          scalars.push(...this.points(command, "points", 3).map((v) => this.fbits(v)));
          if (command.legacy) flagsExtra |= FLAG_TUBE_LEGACY;
          if (command.spline) flagsExtra |= FLAG_SPLINE;
        } else if (kind === "profiletube") {
          const pts = this.points(command, "points", 3);
          const radii = command.radii;
          if (!radii || !radii.length) {
            throw new NotImplementedComponent("meshPrimitive3D profileTube: missing 'radii'");
          }
          scalars = [this.fbits(Math.trunc(pts.length / 3))];
          if ("ringsPerSpan" in command) {
            flagsExtra |= FLAG_TUBE_PATH_DENSITY;
            scalars.push(f("ringsPerSpan"));
          }
          scalars.push(...pts.map((v) => this.fbits(v)), ...fbAll(radii));
          if (command.capped === void 0 || command.capped) flagsExtra |= FLAG_TUBE_CAP;
        } else if (kind === "sweep") {
          const centre = command.center ?? [0, 0, 0];
          if (centre.length !== 3) {
            throw new NotImplementedComponent("meshPrimitive3D sweep: center needs 3 values");
          }
          const bool = (v, d) => this.fbits((v === void 0 ? d : v) ? 1 : 0);
          scalars = [
            bool(command.closed, true),
            bool(command.capStart, true),
            bool(command.capEnd, true),
            ...fbAll(centre)
          ];
          extraChannels.push(this.points(command, "section", 2).map((v) => this.fbits(v)));
          extraChannels.push(this.points(command, "path", 3).map((v) => this.fbits(v)));
          if ("scales" in command) extraChannels.push(fbAll(command.scales));
          if ("twists" in command) {
            if (!("scales" in command)) {
              throw new NotImplementedComponent(
                "meshPrimitive3D sweep: 'twists' requires 'scales' (channel order is positional)"
              );
            }
            extraChannels.push(fbAll(command.twists));
          }
        } else if (kind === "extrudepath") {
          const centre = command.center ?? [0, 0, 0];
          if (centre.length !== 3) {
            throw new NotImplementedComponent(
              "meshPrimitive3D extrudePath: center needs 3 values"
            );
          }
          const contours = command.contours;
          if (!contours || !contours.length) {
            throw new NotImplementedComponent(
              "meshPrimitive3D extrudePath: missing 'contours'"
            );
          }
          scalars = [
            this.fbits(command.depth ?? 1),
            ...fbAll(centre),
            this.fbits(command.bevel ?? 0),
            this.fbits(contours.length)
          ];
          for (const ring of contours) {
            scalars.push(this.fbits(ring.length));
            for (const pt of ring) {
              if (!Array.isArray(pt) || pt.length !== 2) {
                throw new NotImplementedComponent(
                  "meshPrimitive3D extrudePath: contour points are [x, y] pairs"
                );
              }
              scalars.push(this.fbits(pt[0]), this.fbits(pt[1]));
            }
          }
        } else if (kind in PRIMITIVE_PARAMS) {
          scalars = [];
          for (const key of PRIMITIVE_PARAMS[kind]) {
            if (key === "center" || key === "from" || key === "to") {
              const vec = command[key] ?? [0, 0, 0];
              if (vec.length !== 3) {
                throw new NotImplementedComponent(
                  `meshPrimitive3D ${kind}: ${key} needs 3 components`
                );
              }
              scalars.push(...fbAll(vec));
            } else {
              if (!(key in command)) {
                throw new NotImplementedComponent(
                  `meshPrimitive3D ${kind}: missing '${key}' (needs ${JSON.stringify(PRIMITIVE_PARAMS[kind])})`
                );
              }
              scalars.push(this.fbits(command[key]));
            }
          }
        } else {
          throw new NotImplementedComponent(
            `meshPrimitive3D ${kind}: pass an explicit "params" list`
          );
        }
        let flags = Math.trunc(Number(command.flags ?? 0)) | flagsExtra;
        const uvname = String(command.uv ?? "none").toLowerCase();
        if (!(uvname in UV_MODE)) {
          throw new NotImplementedComponent(`meshPrimitive3D uv '${uvname}'`);
        }
        flags |= UV_MODE[uvname] << 4;
        const channels = [scalars, ...extraChannels];
        for (const extra of command.channels ?? []) channels.push(fbAll(extra));
        if (kind === "lathe" && "profile" in command) {
          const prof = [];
          for (const pt of command.profile) {
            if (!Array.isArray(pt) || pt.length !== 2) {
              throw new NotImplementedComponent(
                "meshPrimitive3D lathe: profile points are [radius, y] pairs"
              );
            }
            prof.push(this.fbits(pt[0]), this.fbits(pt[1]));
          }
          channels.push(prof);
        }
        w.meshPrimitive3D(
          Number(command.id),
          ptype,
          this.fbits(command.segments ?? 0),
          flags,
          channels
        );
        return;
      }
      if (t === "meshexpression3d") {
        const surface = String(command.surface ?? "general").toLowerCase().replace(/_/g, "");
        if (!(surface in SURFACE)) {
          throw new NotImplementedComponent(
            `meshExpression3D surface '${command.surface}' (expected one of ${Object.keys(SURFACE).sort().join(", ")})`
          );
        }
        const stype = SURFACE[surface];
        const rpn = (expr) => {
          const saved = {
            u: this.variables.isMaterialised("u") ? this.variables.get("u") : void 0,
            v: this.variables.isMaterialised("v") ? this.variables.get("v") : void 0
          };
          this.variables.set("u", VAR1_BITS);
          this.variables.set("v", VAR2_BITS);
          try {
            return this.expr.infixToRpn(String(expr));
          } finally {
            for (const k of ["u", "v"]) {
              if (saved[k] === void 0) this.variables.remove(k);
              else this.variables.set(k, saved[k]);
            }
          }
        };
        const need = (key) => {
          if (!(key in command)) {
            throw new NotImplementedComponent(
              `meshExpression3D ${surface}: missing '${key}'`
            );
          }
          return command[key];
        };
        const rng = (key, dflt) => {
          const r = command[key] ?? dflt;
          if (!r || r.length !== 2) {
            throw new NotImplementedComponent(
              `meshExpression3D ${surface}: '${key}' must be [min, max]`
            );
          }
          return [this.fbits(r[0]), this.fbits(r[1])];
        };
        let params, pos;
        if (surface === "heightfield") {
          params = [
            ...rng("xRange"),
            this.fbits(need("uCount")),
            ...rng("zRange"),
            this.fbits(need("vCount")),
            ...rng("yRange", [-1e6, 1e6])
          ];
          pos = [rpn(need("height"))];
        } else if (surface === "sphere") {
          params = [
            this.fbits(need("radius")),
            this.fbits(need("uCount")),
            this.fbits(need("vCount"))
          ];
          pos = [rpn(command.displacement ?? 0)];
        } else if (surface === "cylinder") {
          params = [
            this.fbits(need("radius")),
            this.fbits(need("height")),
            this.fbits(need("uCount")),
            this.fbits(need("vCount"))
          ];
          pos = [rpn(command.displacement ?? 0)];
        } else {
          params = [
            ...rng("uRange"),
            this.fbits(need("uCount")),
            ...rng("vRange"),
            this.fbits(need("vCount"))
          ];
          pos = [rpn(need("x")), rpn(need("y")), rpn(need("z"))];
        }
        const normal = (command.normal ?? []).map(rpn);
        if (normal.length && normal.length !== 3) {
          throw new NotImplementedComponent(
            "meshExpression3D: 'normal' needs three expressions (nx, ny, nz); omit it for finite-difference normals"
          );
        }
        const uvg = (command.uv ?? []).map(rpn);
        if (uvg.length && uvg.length !== 2) {
          throw new NotImplementedComponent(
            "meshExpression3D: 'uv' needs two expressions (u, v)"
          );
        }
        let flags = Math.trunc(Number(command.flags ?? 0));
        if (command.flipWinding) flags |= 1;
        w.meshExpression3D(Number(command.id), stype, flags, params, pos, normal, uvg);
        return;
      }
      if (t === "conditionaloperations") {
        const COND = { gt: 4, ge: 5, lt: 2, le: 3, eq: 0, ne: 1 };
        const cond = COND[String(command.condition).toLowerCase()] ?? 0;
        w.conditionalOperations(cond, f("v1"), f("v2"));
        for (const c of command.commands ?? []) this.parseCommand(c);
        w.endConditional();
        return;
      }
      if (t === "loop") {
        const fromBits = f("from");
        const stepBits = "step" in command ? f("step") : floatToRawIntBits(1);
        const untilBits = f("until");
        const index = String(command.index ?? "i");
        const indexId = command.noIndexText ? w.allocDataId() : w.addText(index);
        w.startLoop(indexId, fromBits, stepBits, untilBits);
        const had = this.variables.isMaterialised(index);
        const prev = had ? this.variables.get(index) : void 0;
        this.variables.set(index, (indexId | 4286578688) >>> 0);
        for (const c of command.commands ?? []) this.parseCommand(c);
        if (had && prev !== void 0) this.variables.set(index, prev);
        else this.variables.remove(index);
        w.endLoop();
        return;
      }
      if (t === "cliprect") {
        w.clipRect(f("left"), f("top"), f("right"), f("bottom"));
        return;
      }
      throw new NotImplementedComponent(`canvas command '${t}'`);
    }
    parseVariable(command) {
      const name = String(command.name);
      const vtype = String(command.vtype ?? "float");
      if (vtype !== "float") {
        throw new NotImplementedComponent(`variable vtype '${vtype}'`);
      }
      if (command.export) {
        // esp32-rc patch (not upstream yet): an exported canvas variable, so a value that
        // depends on a canvas command (a touch expression) can be named for the player.
        // Written where it stands, with its NAMED_VARIABLE after it.
        if (this.variables.isMaterialised(name)) return;
        const bits = this.materialiseFloatVar(command);
        this.variables.set(name, bits);
        this.writer.setFloatName(bits & 8388607, name);
        return;
      }
      const commit = Boolean(command.commit), flush = Boolean(command.flush);
      if (!(this.inFirstPass || commit || flush)) {
        this.variables.defer(name, command);
        return;
      }
      if (flush) throw new NotImplementedComponent("flush variable TBD");
      if (this.variables.isMaterialised(name)) {
        throw new NotImplementedComponent("variable reassignment (targetId) TBD");
      }
      this.variables.set(name, this.materialiseFloatVar(command));
    }
    materialiseFloatVar(command) {
      const value = command.value;
      if (typeof value === "number") return this.writer.addFloatConstant(value);
      if (value && typeof value === "object" && !Array.isArray(value)) {
        const kind = String(value.type ?? "").toLowerCase();
        if (kind === "textfromfloat" || kind === "textmerge") {
          return floatToRawIntBits(this.resolveTextId(value));
        }
      }
      return this.fbits(value);
    }
    parseResources(section) {
      const order = section.order ?? _Parser.RESOURCE_ORDER.filter((k) => k in section);
      for (const key of order) {
        if (!(key in section)) continue;
        if (!["colors", "floatArrays", "variables", "bitmaps"].includes(key)) {
          throw new NotImplementedComponent(
            `resource type '${key}' (colors/floatArrays/variables/bitmaps only)`
          );
        }
        this.parseResourceSection(key, section[key]);
      }
    }
    parseResourceSection(key, sectionValue) {
      const section = { [key]: sectionValue };
      if (key === "bitmaps") {
        for (const [name, , value] of eachResource(sectionValue)) {
          this.bitmaps.set(name, this.declareBitmap(name, value));
        }
        return;
      }
      if ("colors" in section) {
        for (const [name, , value] of eachResource(section.colors)) {
          if (value && typeof value === "object" && !Array.isArray(value)) {
            throw new NotImplementedComponent("themed (light/dark) color TBD");
          }
          this.colors.set(name, this.writer.addNamedColor(name, parseColor(value)));
        }
      }
      if ("floatArrays" in section) {
        for (const [name, cfg, value] of eachResource(section.floatArrays)) {
          const raw = value && typeof value === "object" && "value" in value ? value.value : value;
          const data = raw.map(Number);
          const named = cfg === null || cfg.export !== false;
          const aid = named ? this.writer.addNamedFloatArray(name, data) : this.writer.addFloatArray(data);
          this.variables.set(name, (aid | 4286578688) >>> 0);
        }
      }
      if ("variables" in section) {
        for (const [name, cfg, value] of eachResource(section.variables)) {
          const named = cfg ? Boolean(cfg.export) : false;
          if (cfg && String(cfg.type ?? "").toLowerCase() === "string") {
            const text = String(cfg.value ?? "");
            const id = named ? this.writer.addNamedString(name, text) : this.writer.addText(text);
            this.variables.set(name, floatToRawIntBits(id));
            continue;
          }
          let val = value;
          if (val && typeof val === "object" && "value" in val && !("anim" in val)) {
            val = val.value;
          }
          if (val && typeof val === "object" && ["textfromfloat", "textmerge"].includes(String(val.type ?? "").toLowerCase())) {
            this.variables.set(
              name,
              floatToRawIntBits(this.resolveTextId(val))
            );
          } else if (typeof val === "number") {
            this.variables.set(name, named ? this.writer.addNamedFloat(name, val) : this.writer.addFloatConstant(val));
          } else {
            const bits = this.fbits(val);
            this.variables.set(name, bits);
            if (named) this.writer.setFloatName(bits & 8388607, name);
          }
        }
      }
    }
    parseText(component, mods) {
      if (!this.profiles) {
        throw new NotImplementedComponent(
          'text without a `profiles` header tag \u2014 add "profiles": 513'
        );
      }
      const value = component.value;
      let textId;
      if (typeof value === "string" && (value.startsWith("$") || value.startsWith("@"))) {
        textId = this.resolveTextId(value);
      } else if (value !== void 0 && value !== null) {
        textId = this.writer.addText(String(value));
      } else if (component.textFromFloat && typeof component.textFromFloat === "object") {
        const tff = component.textFromFloat;
        textId = this.writer.addTextFromFloat(
          this.fbits(tff.value),
          Number(tff.whole ?? 0),
          Number(tff.decimal ?? 0),
          Number(tff.flags ?? 0)
        );
      } else {
        throw new NotImplementedComponent("text without value or textFromFloat");
      }
      let color = 4278190080;
      let colorId = -1;
      const c = component.color;
      if (typeof c === "string" && (c.startsWith("$colors.") || c.startsWith("@colors."))) {
        colorId = this.resolveColor(c);
      } else if (c !== void 0 && c !== null) {
        color = this.resolveColor(c);
      }
      const overflow = OVERFLOW[String(component.overflow ?? "clip").toLowerCase()] ?? 1;
      this.writer.textComponent(textId, {
        color,
        colorId,
        fontSizeBits: component.fontSize !== void 0 ? this.fbits(component.fontSize) : floatToRawIntBits(36),
        fontWeightBits: component.fontWeight !== void 0 ? this.fbits(component.fontWeight) : floatToRawIntBits(400),
        textAlign: parseTextAlign(String(component.textAlign ?? "start")),
        overflow,
        maxLines: component.maxLines !== void 0 ? Number(component.maxLines) : INT_MAX2,
        modifiers: mods
      });
    }
  };
  /**
   * Resource sections, in the reference's order.
   *
   * The order is load-bearing: ids are allocated as each section is parsed, so a section
   * handled out of turn shifts every id after it. `order` overrides the default, and an
   * unrecognised section raises rather than being skipped — silently ignoring one would
   * produce a document that converts and is missing everything it declared.
   */
  _Parser.RESOURCE_ORDER = [
    "v_dims",
    "colors",
    "paths",
    "floatArrays",
    "variables",
    "integers",
    "matrices",
    "bitmaps",
    "sounds"
  ];
  var Parser = _Parser;
  function unwrap(doc) {
    if (!("root" in doc) && doc.json && typeof doc.json === "object") return doc.json;
    return doc;
  }
  function convert(json, options = {}) {
    const doc = typeof json === "string" ? JSON.parse(json) : json;
    return new Parser(options).parse(unwrap(doc));
  }
  return __toCommonJS(Parser_exports);
})();
