import sqlite3
import os
from datetime import date
from flask import Flask, render_template, request, redirect, url_for, flash, g

app = Flask(__name__)
app.secret_key = 'traffic-mgmt-secret-key-2024'

DATABASE = os.path.join(os.path.dirname(__file__), 'inventory.db')


def get_db():
    db = getattr(g, '_database', None)
    if db is None:
        db = g._database = sqlite3.connect(DATABASE)
        db.row_factory = sqlite3.Row
    return db


@app.teardown_appcontext
def close_connection(exception):
    db = getattr(g, '_database', None)
    if db is not None:
        db.close()


def init_db():
    db = sqlite3.connect(DATABASE)
    db.row_factory = sqlite3.Row
    cursor = db.cursor()

    cursor.executescript('''
        CREATE TABLE IF NOT EXISTS stock_items (
            id INTEGER PRIMARY KEY AUTOINCREMENT,
            name TEXT NOT NULL,
            description TEXT,
            unit TEXT NOT NULL DEFAULT 'units',
            low_stock_threshold INTEGER NOT NULL DEFAULT 10,
            created_at TIMESTAMP DEFAULT CURRENT_TIMESTAMP
        );

        CREATE TABLE IF NOT EXISTS sites (
            id INTEGER PRIMARY KEY AUTOINCREMENT,
            name TEXT NOT NULL,
            location TEXT,
            active INTEGER NOT NULL DEFAULT 1,
            created_at TIMESTAMP DEFAULT CURRENT_TIMESTAMP
        );

        CREATE TABLE IF NOT EXISTS ledger_entries (
            id INTEGER PRIMARY KEY AUTOINCREMENT,
            stock_item_id INTEGER NOT NULL,
            entry_type TEXT NOT NULL CHECK(entry_type IN ('opening', 'addition', 'removal')),
            quantity INTEGER NOT NULL,
            site_id INTEGER,
            date TEXT NOT NULL,
            notes TEXT,
            created_at TIMESTAMP DEFAULT CURRENT_TIMESTAMP,
            FOREIGN KEY (stock_item_id) REFERENCES stock_items(id),
            FOREIGN KEY (site_id) REFERENCES sites(id)
        );
    ''')

    count = db.execute('SELECT COUNT(*) as c FROM stock_items').fetchone()['c']
    if count == 0:
        today = date.today().isoformat()

        cursor.execute(
            "INSERT INTO sites (name, location, active) VALUES (?, ?, 1)",
            ('A406 North Circular Roadworks', 'North Circular Road, London')
        )
        site1_id = cursor.lastrowid

        cursor.execute(
            "INSERT INTO sites (name, location, active) VALUES (?, ?, 1)",
            ('M25 Junction 10 Maintenance', 'M25 J10, Surrey')
        )
        site2_id = cursor.lastrowid

        cursor.execute(
            "INSERT INTO stock_items (name, description, unit, low_stock_threshold) VALUES (?, ?, ?, ?)",
            ('Traffic Cones', 'Standard 750mm traffic cones', 'units', 20)
        )
        cone_id = cursor.lastrowid

        cursor.execute(
            "INSERT INTO stock_items (name, description, unit, low_stock_threshold) VALUES (?, ?, ?, ?)",
            ('Barrier Fencing', 'Plastic interlocking barrier sections', 'metres', 50)
        )
        barrier_id = cursor.lastrowid

        cursor.execute(
            "INSERT INTO stock_items (name, description, unit, low_stock_threshold) VALUES (?, ?, ?, ?)",
            ('LED Warning Lights', 'Battery-powered amber LED warning lights', 'units', 5)
        )
        light_id = cursor.lastrowid

        # Opening stock
        cursor.execute(
            "INSERT INTO ledger_entries (stock_item_id, entry_type, quantity, site_id, date, notes) VALUES (?, 'opening', 100, NULL, ?, 'Opening stock count')",
            (cone_id, today)
        )
        cursor.execute(
            "INSERT INTO ledger_entries (stock_item_id, entry_type, quantity, site_id, date, notes) VALUES (?, 'opening', 200, NULL, ?, 'Opening stock count')",
            (barrier_id, today)
        )
        cursor.execute(
            "INSERT INTO ledger_entries (stock_item_id, entry_type, quantity, site_id, date, notes) VALUES (?, 'opening', 15, NULL, ?, 'Opening stock count')",
            (light_id, today)
        )

        # Additions
        cursor.execute(
            "INSERT INTO ledger_entries (stock_item_id, entry_type, quantity, site_id, date, notes) VALUES (?, 'addition', 50, NULL, ?, 'New delivery from supplier')",
            (cone_id, today)
        )

        # Removals to sites
        cursor.execute(
            "INSERT INTO ledger_entries (stock_item_id, entry_type, quantity, site_id, date, notes) VALUES (?, 'removal', 40, ?, ?, 'Deployed to A406 site')",
            (cone_id, site1_id, today)
        )
        cursor.execute(
            "INSERT INTO ledger_entries (stock_item_id, entry_type, quantity, site_id, date, notes) VALUES (?, 'removal', 30, ?, ?, 'Deployed to M25 site')",
            (cone_id, site2_id, today)
        )
        cursor.execute(
            "INSERT INTO ledger_entries (stock_item_id, entry_type, quantity, site_id, date, notes) VALUES (?, 'removal', 80, ?, ?, 'Barrier run for A406')",
            (barrier_id, site1_id, today)
        )
        cursor.execute(
            "INSERT INTO ledger_entries (stock_item_id, entry_type, quantity, site_id, date, notes) VALUES (?, 'removal', 8, ?, ?, 'Warning lights for M25 site')",
            (light_id, site2_id, today)
        )

        db.commit()

    db.close()


def get_current_stock(db, item_id):
    row = db.execute('''
        SELECT
            COALESCE(SUM(CASE WHEN entry_type IN ('opening', 'addition') THEN quantity ELSE 0 END), 0) -
            COALESCE(SUM(CASE WHEN entry_type = 'removal' THEN quantity ELSE 0 END), 0) as current_stock
        FROM ledger_entries
        WHERE stock_item_id = ?
    ''', (item_id,)).fetchone()
    return row['current_stock'] if row else 0


# ---------------------------------------------------------------------------
# Routes
# ---------------------------------------------------------------------------

@app.route('/')
def dashboard():
    db = get_db()
    items = db.execute('SELECT * FROM stock_items ORDER BY name').fetchall()
    active_sites_count = db.execute(
        'SELECT COUNT(*) as c FROM sites WHERE active = 1'
    ).fetchone()['c']

    items_data = []
    low_stock_count = 0
    for item in items:
        current_stock = get_current_stock(db, item['id'])
        is_low = current_stock <= item['low_stock_threshold']
        if is_low:
            low_stock_count += 1
        items_data.append({
            'id': item['id'],
            'name': item['name'],
            'unit': item['unit'],
            'low_stock_threshold': item['low_stock_threshold'],
            'current_stock': current_stock,
            'is_low': is_low,
        })

    return render_template(
        'dashboard.html',
        items=items_data,
        total_items=len(items_data),
        low_stock_count=low_stock_count,
        active_sites_count=active_sites_count,
    )


@app.route('/items')
def items_list():
    db = get_db()
    items = db.execute('SELECT * FROM stock_items ORDER BY name').fetchall()
    items_data = []
    for item in items:
        current_stock = get_current_stock(db, item['id'])
        is_low = current_stock <= item['low_stock_threshold']
        items_data.append({
            'id': item['id'],
            'name': item['name'],
            'description': item['description'],
            'unit': item['unit'],
            'low_stock_threshold': item['low_stock_threshold'],
            'current_stock': current_stock,
            'is_low': is_low,
        })
    return render_template('items_list.html', items=items_data)


@app.route('/items/new', methods=['GET', 'POST'])
def item_new():
    if request.method == 'POST':
        name = request.form.get('name', '').strip()
        description = request.form.get('description', '').strip()
        unit = request.form.get('unit', 'units').strip() or 'units'
        low_stock_threshold = request.form.get('low_stock_threshold', '10')
        opening_stock = request.form.get('opening_stock', '0')
        notes = request.form.get('notes', '').strip()

        if not name:
            flash('Item name is required.', 'danger')
            return render_template('item_new.html')

        try:
            low_stock_threshold = int(low_stock_threshold)
            opening_stock = int(opening_stock)
            if low_stock_threshold < 0 or opening_stock < 0:
                raise ValueError
        except ValueError:
            flash('Threshold and opening stock must be non-negative whole numbers.', 'danger')
            return render_template('item_new.html')

        db = get_db()
        cursor = db.execute(
            'INSERT INTO stock_items (name, description, unit, low_stock_threshold) VALUES (?, ?, ?, ?)',
            (name, description, unit, low_stock_threshold)
        )
        item_id = cursor.lastrowid

        if opening_stock > 0:
            db.execute(
                "INSERT INTO ledger_entries (stock_item_id, entry_type, quantity, site_id, date, notes) "
                "VALUES (?, 'opening', ?, NULL, ?, ?)",
                (item_id, opening_stock, date.today().isoformat(), notes or 'Opening stock')
            )

        db.commit()
        flash(f'Stock item "{name}" created successfully.', 'success')
        return redirect(url_for('item_detail', item_id=item_id))

    return render_template('item_new.html')


@app.route('/items/<int:item_id>')
def item_detail(item_id):
    db = get_db()
    item = db.execute('SELECT * FROM stock_items WHERE id = ?', (item_id,)).fetchone()
    if not item:
        flash('Item not found.', 'danger')
        return redirect(url_for('items_list'))

    entries = db.execute('''
        SELECT le.*, s.name as site_name
        FROM ledger_entries le
        LEFT JOIN sites s ON le.site_id = s.id
        WHERE le.stock_item_id = ?
        ORDER BY le.date ASC, le.id ASC
    ''', (item_id,)).fetchall()

    running_balance = 0
    entries_with_balance = []
    for entry in entries:
        if entry['entry_type'] in ('opening', 'addition'):
            running_balance += entry['quantity']
        else:
            running_balance -= entry['quantity']
        entries_with_balance.append({
            'id': entry['id'],
            'entry_type': entry['entry_type'],
            'quantity': entry['quantity'],
            'site_name': entry['site_name'],
            'date': entry['date'],
            'notes': entry['notes'],
            'created_at': entry['created_at'],
            'running_balance': running_balance,
        })

    current_stock = running_balance
    is_low = current_stock <= item['low_stock_threshold']

    return render_template(
        'item_detail.html',
        item=item,
        entries=entries_with_balance,
        current_stock=current_stock,
        is_low=is_low,
    )


@app.route('/items/<int:item_id>/add', methods=['GET', 'POST'])
def item_add_stock(item_id):
    db = get_db()
    item = db.execute('SELECT * FROM stock_items WHERE id = ?', (item_id,)).fetchone()
    if not item:
        flash('Item not found.', 'danger')
        return redirect(url_for('items_list'))

    if request.method == 'POST':
        quantity = request.form.get('quantity', '0')
        notes = request.form.get('notes', '').strip()
        entry_date = request.form.get('date', date.today().isoformat())

        try:
            quantity = int(quantity)
            if quantity <= 0:
                raise ValueError
        except ValueError:
            flash('Quantity must be a positive whole number.', 'danger')
            return render_template('item_add_stock.html', item=item, today=date.today().isoformat())

        db.execute(
            "INSERT INTO ledger_entries (stock_item_id, entry_type, quantity, site_id, date, notes) "
            "VALUES (?, 'addition', ?, NULL, ?, ?)",
            (item_id, quantity, entry_date, notes)
        )
        db.commit()
        flash(f'Added {quantity} {item["unit"]} to {item["name"]}.', 'success')
        return redirect(url_for('item_detail', item_id=item_id))

    return render_template('item_add_stock.html', item=item, today=date.today().isoformat())


@app.route('/items/<int:item_id>/remove', methods=['GET', 'POST'])
def item_remove_stock(item_id):
    db = get_db()
    item = db.execute('SELECT * FROM stock_items WHERE id = ?', (item_id,)).fetchone()
    if not item:
        flash('Item not found.', 'danger')
        return redirect(url_for('items_list'))

    sites = db.execute('SELECT * FROM sites WHERE active = 1 ORDER BY name').fetchall()

    if request.method == 'POST':
        quantity = request.form.get('quantity', '0')
        site_id = request.form.get('site_id', '').strip()
        notes = request.form.get('notes', '').strip()
        entry_date = request.form.get('date', date.today().isoformat())

        try:
            quantity = int(quantity)
            if quantity <= 0:
                raise ValueError
        except ValueError:
            flash('Quantity must be a positive whole number.', 'danger')
            return render_template(
                'item_remove_stock.html', item=item, sites=sites, today=date.today().isoformat()
            )

        if not site_id:
            flash('Please select a site.', 'danger')
            return render_template(
                'item_remove_stock.html', item=item, sites=sites, today=date.today().isoformat()
            )

        current_stock = get_current_stock(db, item_id)
        if quantity > current_stock:
            flash(
                f'Cannot remove {quantity} {item["unit"]} — only {current_stock} in stock.',
                'danger'
            )
            return render_template(
                'item_remove_stock.html', item=item, sites=sites, today=date.today().isoformat()
            )

        db.execute(
            "INSERT INTO ledger_entries (stock_item_id, entry_type, quantity, site_id, date, notes) "
            "VALUES (?, 'removal', ?, ?, ?, ?)",
            (item_id, quantity, site_id, entry_date, notes)
        )
        db.commit()

        site = db.execute('SELECT name FROM sites WHERE id = ?', (site_id,)).fetchone()
        site_name = site['name'] if site else 'unknown site'
        flash(f'Removed {quantity} {item["unit"]} from {item["name"]} to {site_name}.', 'success')
        return redirect(url_for('item_detail', item_id=item_id))

    return render_template(
        'item_remove_stock.html', item=item, sites=sites, today=date.today().isoformat()
    )


@app.route('/sites')
def sites_list():
    db = get_db()
    sites = db.execute('SELECT * FROM sites ORDER BY active DESC, name').fetchall()
    sites_data = []
    for site in sites:
        total_allocated = db.execute('''
            SELECT COALESCE(SUM(quantity), 0) as total
            FROM ledger_entries
            WHERE site_id = ? AND entry_type = 'removal'
        ''', (site['id'],)).fetchone()['total']
        sites_data.append({
            'id': site['id'],
            'name': site['name'],
            'location': site['location'],
            'active': site['active'],
            'total_allocated': total_allocated,
        })
    return render_template('sites_list.html', sites=sites_data)


@app.route('/sites/new', methods=['GET', 'POST'])
def site_new():
    if request.method == 'POST':
        name = request.form.get('name', '').strip()
        location = request.form.get('location', '').strip()
        active = 1 if request.form.get('active') else 0

        if not name:
            flash('Site name is required.', 'danger')
            return render_template('site_new.html')

        db = get_db()
        cursor = db.execute(
            'INSERT INTO sites (name, location, active) VALUES (?, ?, ?)',
            (name, location, active)
        )
        db.commit()
        flash(f'Site "{name}" created successfully.', 'success')
        return redirect(url_for('site_detail', site_id=cursor.lastrowid))

    return render_template('site_new.html')


@app.route('/sites/<int:site_id>')
def site_detail(site_id):
    db = get_db()
    site = db.execute('SELECT * FROM sites WHERE id = ?', (site_id,)).fetchone()
    if not site:
        flash('Site not found.', 'danger')
        return redirect(url_for('sites_list'))

    items_at_site = db.execute('''
        SELECT si.id, si.name, si.unit, si.low_stock_threshold,
               COALESCE(SUM(le.quantity), 0) as qty_at_site
        FROM stock_items si
        LEFT JOIN ledger_entries le
               ON le.stock_item_id = si.id
              AND le.site_id = ?
              AND le.entry_type = 'removal'
        GROUP BY si.id, si.name, si.unit, si.low_stock_threshold
        HAVING qty_at_site > 0
        ORDER BY si.name
    ''', (site_id,)).fetchall()

    return render_template('site_detail.html', site=site, items=items_at_site)


if __name__ == '__main__':
    init_db()
    app.run(debug=True, host='0.0.0.0', port=5000)
