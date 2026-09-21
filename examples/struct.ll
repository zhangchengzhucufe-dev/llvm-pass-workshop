; GEP addressing demo: a mixed-size struct (field alignment -> padding)
%struct.Point = type { i8, i32, i64, i8 }
define i64 @get_x(ptr %p) {
entry:
  %fx = getelementptr %struct.Point, ptr %p, i32 0, i32 1
  %v = load i64, ptr %fx
  %arr = getelementptr i64, ptr %p, i64 3   ; array-style stepping: 3 * 8 bytes
  ret i64 %v
}
