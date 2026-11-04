package com.velaride.companion;

import java.nio.charset.StandardCharsets;
import java.util.Arrays;

public final class RoutePlanTest {
    static void check(boolean value) { if(!value)throw new AssertionError(); }
    static byte[] xml(String text) { return text.getBytes(StandardCharsets.UTF_8); }
    public static void main(String[] args) throws Exception {
        RoutePlan route=RoutePlan.parse(xml("<gpx xmlns='http://www.topografix.com/GPX/1/1'><rte>"
                +"<rtept lat='30' lon='120'/><rtept lat='30.001' lon='120'/><rtept lat='30.001' lon='120.001'/></rte></gpx>"));
        check(route.size()==3);
        RoutePlan.Hint start=route.update(29.999,120,5);
        check(start.direction==5 && start.distanceM>100 && !start.offRoute);
        RoutePlan.Hint right=route.update(30,120,5);
        check(right.direction==2 && right.distanceM>100);
        RoutePlan.Hint off=route.update(30,120.003,5);
        check(off.offRoute);
        RoutePlan.Hint straight=route.update(30.001,120,5);
        check(straight.direction==0 && straight.index==3);
        RoutePlan.Hint end=route.update(30.001,120.001,5);
        check(end.direction==4 && end.distanceM==0);
        route.restart();check(route.update(29.999,120,5).direction==5);
        RoutePlan left=new RoutePlan(Arrays.asList(new RoutePlan.Point(30,120),new RoutePlan.Point(30.001,120),new RoutePlan.Point(30.001,119.999)));
        check(left.update(30,120,5).direction==1);
        for(String bad:new String[]{"<gpx/>","<other/>","<!DOCTYPE gpx [<!ENTITY x SYSTEM 'file:///etc/passwd'>]><gpx/>",
                "<gpx><rte><rtept lat='NaN' lon='1'/><rtept lat='1' lon='1'/></rte></gpx>",
                "<gpx><trk><trkseg/><trkseg/></trk></gpx>"}) {
            try {RoutePlan.parse(xml(bad));throw new AssertionError();}catch(Exception expected){}
        }
        try {route.update(30,120,100);throw new AssertionError();}catch(IllegalArgumentException expected){}
        System.out.println("PASS: GPX validation, start/right/left/arrival, restart, off-route segments, accuracy and DTD/multi-segment rejection");
    }
}
