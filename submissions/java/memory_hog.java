import java.util.List;
import java.util.ArrayList;

public class Main {
    public static void main(String[] args) {
        List<byte[]> chunks = new ArrayList<>();
        while (true) {
            chunks.add(new byte[1024 * 1024]);
        }
    }
}
