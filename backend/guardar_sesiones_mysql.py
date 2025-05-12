# Archivo: guardar_sesiones_mysql.py
import json
import serial  # pip install pyserial
import mysql.connector
from mysql.connector import errorcode

# Configuración de serial (puerto y baudios)
SERIAL_PORT = '/dev/COM3'
BAUD_RATE = 115200

# Configuración de MySQL
db_config = {
    'user': 'root',
    'password': 'jorgendo43',
    'host': 'localhost',
    'database': 'concentracion',
    'raise_on_warnings': True
}

# Conexión a MySQL
def crear_conexion():
    try:
        conn = mysql.connector.connect(**db_config)
        return conn
    except mysql.connector.Error as err:
        if err.errno == errorcode.ER_ACCESS_DENIED_ERROR:
            print("Usuario o contraseña incorrectos")
        elif err.errno == errorcode.ER_BAD_DB_ERROR:
            print("La base de datos no existe")
        else:
            print(err)
        raise

# Inserta o recupera Id de Usuario/Materia
def get_or_create(cursor, tabla, nombre):
    campo_id = 'Id_' + tabla
    cursor.execute(f"SELECT {campo_id} FROM {tabla} WHERE Nombre = %s", (nombre,))
    fila = cursor.fetchone()
    if fila:
        return fila[0]
    cursor.execute(f"INSERT INTO {tabla}(Nombre) VALUES (%s)", (nombre,))
    return cursor.lastrowid

# Inserta la sesión en la BD
def guardar_sesion(conn, data):
    cursor = conn.cursor()
    # Usuario
    id_usuario = get_or_create(cursor, 'Usuario', data['nombre'])
    # Materia
    id_materia = get_or_create(cursor, 'Materia', data['materia'])
    # Sesión
    insert_sql = (
        "INSERT INTO Sesion ("
        "Id_Usuario, Id_Materia, DuracionDeseada, DuracionFinal,"
        "Distracciones, LuzPromedio, TemperaturaPromedio, FechaYHora,"
        "RuidoAmbiental, AlarmasSonadas)"
        "VALUES (%s, %s, %s, %s, %s, %s, %s, %s, %s, %s)"
    )
    valores = (
        id_usuario,
        id_materia,
        data['duracionDeseada'],
        data['duracionFinal'],
        data['distracciones'],
        data['luz_promedio'],
        data['temperatura_promedio'],
        data['fecha'],
        data.get('distracciones_movimiento', 0) + data.get('distracciones_sonido', 0),
        data.get('alarmasSonadas', 0)
    )
    cursor.execute(insert_sql, valores)
    conn.commit()
    cursor.close()
    print(f"Sesión guardada: Usuario={data['nombre']}, Materia={data['materia']}")

# Bucle principal: escucha por serial
if __name__ == '__main__':
    conn = crear_conexion()
    ser = serial.Serial(SERIAL_PORT, BAUD_RATE, timeout=1)
    print("Esperando datos desde Arduino...")
    try:
        while True:
            line = ser.readline().decode('utf-8').strip()
            if line.startswith('DB_DATA:'):
                payload = line[len('DB_DATA:'):]
                try:
                    data = json.loads(payload)
                    guardar_sesion(conn, data)
                except Exception as e:
                    print(f"Error procesando línea: {line}\n{e}")
    except KeyboardInterrupt:
        print("Deteniendo escucha serial...")
        ser.close()
        conn.close()