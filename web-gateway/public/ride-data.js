// Shared by the browser and API so both accept the same ride records.
export function validateRide(input) {
  if (!input || typeof input !== 'object' || Array.isArray(input)) {
    throw new Error('骑行记录必须是对象');
  }
  const fields = {
    elapsed_s: '总时长', moving_s: '运动时长', still_s: '静止时长',
    pause_count: '暂停次数', impacts: '冲击次数', distance_km: '距离',
  };
  const ride = {};
  for (const [key, label] of Object.entries(fields)) {
    const value = input[key] === undefined ? 0 : input[key];
    if (typeof value !== 'number' || !Number.isFinite(value) || value < 0 ||
        value > Number.MAX_SAFE_INTEGER) {
      throw new Error(`${label}必须是有效的非负数字`);
    }
    if (key !== 'distance_km' && !Number.isSafeInteger(value)) {
      throw new Error(`${label}必须是整数`);
    }
    ride[key] = value;
  }
  if (ride.elapsed_s <= 0) throw new Error('请先填写手表显示的真实总时长');
  if (ride.moving_s + ride.still_s > ride.elapsed_s) {
    throw new Error('运动时长与静止时长之和不能大于总时长');
  }
  const integer = (key, max = 0xffffffff) => {
    const value = input[key];
    if (!Number.isSafeInteger(value) || value < 0 || value > max) throw new Error(`${key} 数值异常`);
    return value;
  };
  if (input.rest_count !== undefined) ride.rest_count = integer('rest_count');
  const gps = ['gps_time_ms', 'gps_distance_m', 'gps_avg_centi', 'gps_max_centi'];
  if (gps.some((key) => input[key] !== undefined)) {
    for (const key of gps) ride[key] = integer(key, key.endsWith('centi') ? 20000 : 0xffffffff);
    if (ride.gps_time_ms === 0 && gps.slice(1).some((key) => ride[key] !== 0)) throw new Error('缺失 GPS 不能包含统计');
    if (input.distance_km === undefined) ride.distance_km = ride.gps_distance_m / 1000;
    if (Math.abs(ride.distance_km - ride.gps_distance_m / 1000) > 0.000001) throw new Error('GPS 距离字段不一致');
  }
  const safety = ['safety_version', 'safety_checks', 'safety_confirmed', 'safety_pauses',
    'safety_help', 'safety_unanswered', 'safety_last_action', 'safety_last_elapsed_s'];
  if (safety.some((key) => input[key] !== undefined)) {
    for (const key of safety) ride[key] = integer(key, key === 'safety_last_elapsed_s' ? 0xffffffff : 65535);
    if (ride.safety_version > 1 || ride.safety_last_action > 4 ||
        ride.safety_confirmed + ride.safety_pauses + ride.safety_help > ride.safety_checks ||
        ride.safety_unanswered > ride.safety_checks || ride.safety_last_elapsed_s > ride.elapsed_s ||
        (ride.safety_version === 0 && safety.slice(1).some((key) => ride[key] !== 0)))
      throw new Error('安全确认统计不一致');
  }
  return ride;
}
