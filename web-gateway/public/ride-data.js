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
  return ride;
}
