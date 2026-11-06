package com.velaride.companion;

public final class ParkingSpotTest {
    private static void expect(boolean ok, String what) { if (!ok) throw new AssertionError(what); }

    public static void main(String[] args) {
        // Bike parked ~111 m due north of the rider.
        ParkingSpot spot = new ParkingSpot(31.001, 121.0, 8, 1000, true);
        ParkingSpot.Guide north = spot.guide(31.0, 121.0, 5, 0);
        expect(Math.abs(north.metres - 111) <= 1 && north.direction == ParkingSpot.STRAIGHT, "ahead " + north.metres);
        expect(spot.guide(31.0, 121.0, 5, 90).direction == ParkingSpot.LEFT, "facing east: bike on the left");
        expect(spot.guide(31.0, 121.0, 5, 270).direction == ParkingSpot.RIGHT, "facing west: bike on the right");
        expect(spot.guide(31.0, 121.0, 5, 180).direction == ParkingSpot.BEHIND, "facing south: behind");
        expect(spot.guide(31.0, 121.0, 5, 350).direction == ParkingSpot.STRAIGHT, "wraps across north");
        ParkingSpot.Guide blind = spot.guide(31.0, 121.0, 5, Double.NaN);
        expect(blind.direction == ParkingSpot.UNKNOWN && blind.text().startsWith("北方向"), "no heading: cardinal " + blind.text());
        expect(spot.guide(31.00095, 121.0, 5, 0).arrived(), "within 15 m arrives");
        expect(!spot.guide(31.0, 121.0, 200, 0).arrived(), "poor fix still capped at 40 m");
        expect(ParkingSpot.cardinal(44).equals("东北") && ParkingSpot.cardinal(-90).equals("西"), "cardinal names");
        expect(ParkingSpot.distance(1530).equals("1.5 公里") && ParkingSpot.distance(87).equals("87 米"), "distance text");

        ParkingSpot back = ParkingSpot.decode(spot.encode());
        expect(back != null && Math.abs(back.lat - 31.001) < 1e-7 && back.auto && back.savedMs == 1000, "round trip");
        expect(ParkingSpot.decode("") == null && ParkingSpot.decode("x,y") == null
                && ParkingSpot.decode("0,0,5,1,0") == null && ParkingSpot.decode("91,0,5,1,0") == null, "damaged rejected");
        System.out.println("ParkingSpotTest passed");
    }
}
